static func<void> main() {
    long[][] inputs = [grabRandomArray(), grabRandomArray()];
    pinvokeFunction(inputs);
    wasmFunction(inputs);
    mmFunction(inputs);
}

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

    public fixed long Input[256];

    public fixed long Output[256];
}

static tooth<void> printArray(long[] array) language("CSHARP") => |
    Console.WriteLine($"[{string.Join(',', array)}]");
|

static tooth<long[]> grabRandomArray() language("CSHARP") => |
    Random rand = new Random();
    long[] numbers = new long[256];
    for (int i = 0; i < numbers.Length; i++) {
        numbers[i] = (long) rand.Next(1, 1000);
    }
    return numbers;
|

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

            foreach (long[] input in inputs) {

                unsafe {

                    for (int i = 0; i < 256; i++) {
                        data.Input[i] = input[i];
                    }
                }
                WriteStructToAccessor(accessor, ref data);

                sem_post(semClient);
                sem_wait(semReceiver);

                data = ReadStructFromAccessor<SharedData>(accessor);
                long[] outputArray = new long[256];
                unsafe {
                    for (int i = 0; i < 256; i++) {
                        outputArray[i] = data.Output[i];
                    }
                }
                printArray(outputArray);
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
    int outputOffset = 4096 + (256 * sizeof(long));

    foreach(long[] input in inputs) {
        Span<long> inputSpan = memory.GetSpan<long>(inputOffset, 256);
        input.AsSpan().CopyTo(inputSpan);

        int result = wasmFunc(inputOffset, outputOffset);

        Span<long> outputSpan = memory.GetSpan<long>(outputOffset, 256);
        long[] output = outputSpan.ToArray();
        printArray(input);
        printArray(output);
    }

|

static tooth<void> pinvokeFunction(long[][] inputs) language("CSHARP") => |
    foreach(long[] input in inputs) {
        long[] output = new long[256];
        cFunction(input, output);
        printArray(output);
    }
|

tooth<int> cFunction(long[] Input, long[] Output) language("C") => |
    for (int i = 0; i < 256; i++) {
        Output[i] = Input[i] * i;
    }
    return 0;
|

tooth<void> print(string s) language("C") => |
    if (s != NULL) {
        printf("Received from C#: %s\n", s);
    }
|