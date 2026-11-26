private static int[] checkpoints = [10, 100, 1000, 10000];
private static Stopwatch sw = new Stopwatch();
private static double overhead;
private const int arraySize = 16;
private const int total_runs = 5;

static func<void> main() {
    long[][] inputs = makeInputs(10000, arraySize);
    overhead = getOverheadOfStopWatch();

    Console.WriteLine($"\nOverhead: {overhead}");
    Console.WriteLine($"Ticks -> Nanoseconds: {1000000000 / Stopwatch.Frequency}");

    Console.WriteLine("\nP/Invoke:");
    pinvokeFunction(inputs);

    Console.WriteLine("\nWASM:");
    wasmFunction(inputs);

    Console.WriteLine("\nIPC:");
    mmFunction(inputs);
}

static tooth<double> getOverheadOfStopWatch() language("CSHARP") => |
    for (int i = 0; i < 100000; i++) {
        sw.Restart();
        sw.Stop();
    }

    long sum = 0;
    for (int i = 0; i < 100000; i++) {
        sw.Restart();
        sw.Stop();
        sum += sw.ElapsedTicks;
    }
    return (double) sum / 100000;
|

[DllImport("libc", SetLastError = true, CharSet = CharSet.Ansi)]
private static extern IntPtr sem_open(string name, int oflag, uint mode, uint value);

[DllImport("libc", SetLastError = true)]
private static extern int sem_wait(IntPtr sem);

[DllImport("libc", SetLastError = true)]
private static extern int sem_post(IntPtr sem);

[DllImport("libc", SetLastError = true)]
private static extern int sem_close(IntPtr sem);

[DllImport("libc", SetLastError = true, CharSet = CharSet.Ansi)]
private static extern int sem_unlink(string name);

[StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
public unsafe struct SharedData {
    public int Flag;

    public fixed long Input[arraySize];

    public fixed long Output[arraySize];
}

static tooth<void> printArray(long[] array) language("CSHARP") => |
    Console.WriteLine($"[{string.Join(',', array)}]");
|

static tooth<long[]> grabRandomArray(int size) language("CSHARP") => |
    Random rand = new Random();
    long[] numbers = new long[size];
    for (int i = 0; i < size; i++) {
        numbers[i] = (long) rand.Next(1, 1000);
    }
    return numbers;
|

static func<long[][]> makeInputs(int numberOfInputs, int sizeOfInputs) {
    long[][] inputs = new long[numberOfInputs][];
    for (int i = 0; i < numberOfInputs; i++) {
        inputs[i] = grabRandomArray(sizeOfInputs);
    }
    return inputs;
}

static tooth<void> mmFunction(long[][] inputs) language("CSHARP") => |

    void WriteStructToAccessor<T>(MemoryMappedViewAccessor accessor, ref T structure) where T : struct
    {
        int size = Marshal.SizeOf<T>();
        IntPtr ptr = Marshal.AllocHGlobal(size);
        try
        {
            Marshal.StructureToPtr(structure, ptr, false);
            byte[] bytes = new byte[size];
            Marshal.Copy(ptr, bytes, 0, size);
            accessor.WriteArray(0, bytes, 0, size);
        }
        finally
        {
            Marshal.FreeHGlobal(ptr);
        }
    }

    T ReadStructFromAccessor<T>(MemoryMappedViewAccessor accessor) where T : struct
    {
        int size = Marshal.SizeOf<T>();
        byte[] bytes = new byte[size];
        accessor.ReadArray(0, bytes, 0, size);

        IntPtr ptr = Marshal.AllocHGlobal(size);
        try
        {
            Marshal.Copy(bytes, 0, ptr, size);
            return Marshal.PtrToStructure<T>(ptr);
        }
        finally
        {
            Marshal.FreeHGlobal(ptr);
        }
    }

    string mapname = "/dev/shm/VeneerRuntime";
    long capacity;
    unsafe {
        capacity = sizeof(SharedData);
    };

    IntPtr semClient = sem_open("/VeneerRuntimeClient", 0x40, 0x1B6, 0);
    IntPtr semReceiver = sem_open("/VeneerRuntimeReceiver", 0x40, 0x1B6, 0);
    
    using (MemoryMappedFile mmf = MemoryMappedFile.CreateFromFile(mapname, FileMode.OpenOrCreate, null, capacity, MemoryMappedFileAccess.ReadWrite)) {
        using (MemoryMappedViewAccessor accessor = mmf.CreateViewAccessor())
        {
            SharedData data = new SharedData { Flag = 0 };
            WriteStructToAccessor(accessor, ref data);
            sem_post(semClient);
            sem_wait(semReceiver);

            for (int k = 0; k < total_runs; k++) {
                int index = 0;
                long[] recordedCheckPoints = new long[checkpoints.Length];
                sw.Restart();
                for (int j = 0; j < inputs.Length; j++) {
                    long[] input = inputs[j];

                    unsafe {
                        for (int i = 0; i < arraySize; i++) {
                            data.Input[i] = input[i];
                        }
                    }
                    WriteStructToAccessor(accessor, ref data);

                    sem_post(semClient);
                    sem_wait(semReceiver);

                    data = ReadStructFromAccessor<SharedData>(accessor);
                    long[] outputArray = new long[arraySize];
                    unsafe {
                        for (int i = 0; i < arraySize; i++) {
                            outputArray[i] = data.Output[i];
                        }
                    }

                    if (j + 1 == checkpoints[index]) {
                        recordedCheckPoints[index] = sw.ElapsedTicks;
                        index += 1;
                    }
                }
                sw.Stop();
                printArray(recordedCheckPoints);
            }

            data.Flag = 1;
            WriteStructToAccessor(accessor, ref data);
            sem_post(semClient);
            Thread.Sleep(100);
            
            sem_close(semClient);
            sem_close(semReceiver);
            sem_unlink("/VeneerRuntimeClient");
            sem_unlink("/VeneerRuntimeReceiver");
        }
    }

    if (File.Exists(mapname)) File.Delete(mapname);
|

static tooth<void> wasmFunction(long[][] inputs) language("CSHARP") => |
    using var engine = new Engine();
    using var module = Module.FromFile(engine, "output.wasm");
    using var linker = new Linker(engine);
    using var store = new Store(engine);

    var instance = linker.Instantiate(store, module);

    var memory = instance.GetMemory("memory") 
        ?? throw new Exception("Module did not export linear memory.");

    var wasmFunc = instance.GetFunction<int, int, int>("wasmFunction") 
        ?? throw new Exception("wasmFunction not found.");

    int inputOffset = 4096;
    int outputOffset = 4096 + (arraySize * sizeof(long));

    for (int k = 0; k < total_runs; k++) {
        int index = 0;
        long[] recordedCheckPoints = new long[checkpoints.Length];
        sw.Restart();
        for (int i = 0; i < inputs.Length; i++) {
            long[] input = inputs[i];
            Span<long> inputSpan = memory.GetSpan<long>(inputOffset, arraySize);
            input.AsSpan().CopyTo(inputSpan);

            int result = wasmFunc(inputOffset, outputOffset);

            Span<long> outputSpan = memory.GetSpan<long>(outputOffset, arraySize);
            long[] output = outputSpan.ToArray();
            if (i + 1 == checkpoints[index]) {
                recordedCheckPoints[index] = sw.ElapsedTicks;
                index += 1;
            }
        }
        sw.Stop();
        printArray(recordedCheckPoints);
    }
|

static tooth<void> pinvokeFunction(long[][] inputs) language("CSHARP") => |
    for (int k = 0; k < total_runs; k++) {
        int index = 0;
        long[] recordedCheckPoints = new long[checkpoints.Length];
        sw.Restart();
        for (int i = 0; i < inputs.Length; i++) {
            long[] input = inputs[i];
            long[] output = new long[input.Length];
            cFunction(input, output, output.Length);
            if (i + 1 == checkpoints[index]) {
                recordedCheckPoints[index] = sw.ElapsedTicks;
                index += 1;
            }
        }
        sw.Stop();
        printArray(recordedCheckPoints);
    }
|

tooth<int> cFunction(long[] Input, long[] Output, int size) language("C") => |
    for (int i = 0; i < size; i++) {
        Output[i] = Input[i] * i;
    }
    return 0;
|

tooth<void> print(string s) language("C") => |
    if (s != NULL) {
        printf("Received from C#: %s\n", s);
    }
|