static func<void> main() {
    string s = "Testing right now";
    print(s);
    sendByMM(s);
}

static tooth<void> sendByMM(string s) language("CSHARP") => {
    string mapname = "/dev/shm/VeneerRuntime";
    long capacity = 1024;
    
    using (MemoryMappedFile mmf = MemoryMappedFile.CreateFromFile(mapname, FileMode.OpenOrCreate, null, capacity, MemoryMappedFileAccess.ReadWrite)) {
        using (MemoryMappedViewAccessor accessor = mmf.CreateViewAccessor())
        {
            string message = s;
            byte[] data = Encoding.UTF8.GetBytes(message);

            // Write length (4 bytes), then write the content string
            accessor.Write(0, data.Length);
            accessor.WriteArray(4, data, 0, data.Length);

            Console.WriteLine($"Data written to {mapname}");
            Console.WriteLine("Press Enter to exit and free memory...");
            Console.ReadLine(); 
        }
    }

    if (File.Exists(mapname)) File.Delete(mapname);
}

tooth<void> print(string s) language("C") => |
    if (s != NULL) {
        printf("Received from C#: %s\n", s);
    }
|