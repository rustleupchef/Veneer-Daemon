static func<void> main() {
    string s = "Testing right now";
    print(s);
    sendByMM(s);
}

[StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
public struct SharedData {
    public int Flag;

    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
    public string Input;

    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
    public string Output;
}

static tooth<void> sendByMM(string s) language("CSHARP") => |

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
    long capacity = 1024;
    
    using (MemoryMappedFile mmf = MemoryMappedFile.CreateFromFile(mapname, FileMode.OpenOrCreate, null, capacity, MemoryMappedFileAccess.ReadWrite)) {
        using (MemoryMappedViewAccessor accessor = mmf.CreateViewAccessor())
        {
            SharedData data = new SharedData { Flag = 0, Input = "", Output = "" };
            WriteStructToAccessor(accessor, ref data);

            bool isRunning = true;
            while (isRunning) {
                string input = Console.ReadLine();

                data.Input = input;
                data.Output = "";
                data.Flag = 1;
                WriteStructToAccessor(accessor, ref data);

                while (true) {
                    data = ReadStructFromAccessor<SharedData>(accessor);
                    if (data.Flag == 2) break;
                    if (data.Flag == 3) {
                        isRunning = false;
                        break;
                    }
                    Thread.Sleep(10);
                }

                Console.WriteLine($"Data reponded: {data.Output}");

                data.Flag = 0;
                WriteStructToAccessor(accessor, ref data);
            }
        }
    }

    if (File.Exists(mapname)) File.Delete(mapname);
|

tooth<void> print(string s) language("C") => |
    if (s != NULL) {
        printf("Received from C#: %s\n", s);
    }
|