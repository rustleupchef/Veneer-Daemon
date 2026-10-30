class CreateConnection {
    public string name { get; private set; }

    public string[] inputs { get; private set; }
    public long[][] inputDimensions { get; private set; }

    public string[] outputs { get; private set; }
    public long[][] outputDimensions { get; private set; }

    public CreateConnection (
        string name,
        string[] inputs, 
        long[][] inputDimensions, 
        string[] outputs, 
        long[][] outputDimensions
    ) {
        this.name = name;
        this.inputs = inputs;
        this.inputDimensions = inputDimensions;
        this.outputs = outputs;
        this.outputDimensions = outputDimensions;
    }
}

class DeleteConnection {
    public string name { get; private set; }

    public DeleteConnection(string name) {
        this.name = name;
    }
}

[StructLayout(LayoutKind.Sequential, Pack = 1)]
struct SchemaHeader
{
    public long InputByteSize;
    public long OutputByteSize;
    public long InputMatrixRows;
    public long InputMatrixCols;
    public long OutputMatrixRows;
    public long OutputMatrixCols;
}

static func<long[,]> ConvertToMatrix(long[][] dimensions) {
    long firstSize = dimensions.Length;
    long secondSize = -1;

    for (int i = 0; i < firstSize; i++) {
        if (dimensions[i].Length > secondSize) {
            secondSize = dimensions[i].Length;
        }
    }

    long[,] matrix = new long[firstSize, secondSize];
    for (int i = 0; i < firstSize; i++) {
        for (int j = 0; j < secondSize; j++) {
            if (dimensions[i].Length <= j) break;
            matrix[i, j] = dimensions[i][j];
        }
    }

    return matrix;
}

static func<byte[]> ConvertTypesToBytes(string[] types) {
    byte TypeToByte(string type) {
        switch(type) {
            case "char":
                return 1;
            case "int":
                return 2;
            case "float":
                return 3;
            case "double":
                return 4;
            default:
                return 0;
        }
    }

    byte[] byteTypes = new byte[types.Length];
    for (int i = 0; i < types.Length; i++) {
        byteTypes[i] = TypeToByte(types[i]);
    }

    return byteTypes;
}

static async tooth<void> Server() language("CSHARP") => |
    string directory = "/dev/shm";

    using HttpListener listener = new HttpListener();
    listener.Prefixes.Add("http://localhost:31415/");

    listener.Start();
    while (true) {
        HttpListenerContext context = await listener.GetContextAsync();
        HttpListenerRequest request = context.Request;
        HttpListenerResponse response = context.Response;

        string path = request.Url.LocalPath;
        string method = request.HttpMethod.ToUpper();
        string payload = default(string);

        if (request.HasEntityBody) {
            using (Stream bodyStream = request.InputStream)
            using (StreamReader reader = new StreamReader(bodyStream, request.ContentEncoding)) {
                payload = await reader.ReadToEndAsync();
            }
        }

        if (method == "HEAD") {
            byte[] buffer = Encoding.UTF8.GetBytes("{\"status\" : \"success\"}");
            response.ContentLength64 = buffer.Length;
            response.ContentType = "application/json";

            using var output = response.OutputStream;
            await output.WriteAsync(buffer, 0, buffer.Length);
            output.Close();
        }

        if (method == "POST") {
            if (path == "/create-channel") {
                byte[] buffer;
                if (string.IsNullOrEmpty(payload)) {
                    buffer = Encoding.UTF8.GetBytes("{\"status\" : \"error\"}");
                } else {
                    CreateConnection createConnection = JsonSerializer.Deserialize<CreateConnection>(payload);
                    string name = createConnection.name;

                    string connectionDirectory = Path.Join(directory, name);
                    Directory.CreateDirectory(connectionDirectory);

                    using (FileStream fs = new FileStream(Path.Join(connectionDirectory, "Schema"), FileMode.Create, FileAccess.ReadWrite, FileShare.ReadWrite)) 
                    {                         
                        byte[] inputBytes = ConvertTypesToBytes(createConnection.inputs);                         
                        long[,] inputMatrixDimensions = ConvertToMatrix(createConnection.inputDimensions);                          
                        byte[] outputBytes = ConvertTypesToBytes(createConnection.outputs);                         
                        long[,] outputMatrixDimensions = ConvertToMatrix(createConnection.outputDimensions);                          

                        // Extract matrix structural dimensions
                        long inputRows = inputMatrixDimensions.GetLength(0);
                        long inputCols = inputMatrixDimensions.GetLength(1);
                        long outputRows = outputMatrixDimensions.GetLength(0);
                        long outputCols = outputMatrixDimensions.GetLength(1);

                        // Calculate discrete data block payloads
                        long headerSize = Marshal.SizeOf<SchemaHeader>();
                        long inputByteSize = inputBytes.Length;
                        long outputByteSize = outputBytes.Length;
                        long inputMatrixSize = inputMatrixDimensions.Length * sizeof(long);
                        long outputMatrixSize = outputMatrixDimensions.Length * sizeof(long);

                        // Total allocation bounds required for mapping
                        long totalSize = headerSize + inputByteSize + outputByteSize + inputMatrixSize + outputMatrixSize;
                        
                        // Set file size on disk before creating the memory mapping
                        fs.SetLength(totalSize);

                        // Instantiate header schema definition
                        SchemaHeader header = new SchemaHeader
                        {
                            InputByteSize = inputByteSize,
                            OutputByteSize = outputByteSize,
                            InputMatrixRows = inputRows,
                            InputMatrixCols = inputCols,
                            OutputMatrixRows = outputRows,
                            OutputMatrixCols = outputCols
                        };

                        // Create the MMF mapping directly from the existing FileStream instance
                        using (MemoryMappedFile mmf = MemoryMappedFile.CreateFromFile(
                            fs, 
                            mapName: null, 
                            capacity: totalSize, 
                            MemoryMappedFileAccess.ReadWrite, 
                            HandleInheritability.None, 
                            leaveOpen: false))
                        using (MemoryMappedViewAccessor accessor = mmf.CreateViewAccessor(0, totalSize, MemoryMappedFileAccess.Write))
                        {
                            long writeOffset = 0;

                            // Write metadata header block
                            accessor.Write(writeOffset, ref header);
                            writeOffset += headerSize;

                            // Write input linear byte array 
                            accessor.WriteArray(writeOffset, inputBytes, 0, inputBytes.Length);
                            writeOffset += inputByteSize;

                            // Write output linear byte array
                            accessor.WriteArray(writeOffset, outputBytes, 0, outputBytes.Length);
                            writeOffset += outputByteSize;

                            // Flatten and write multi-dimensional input matrix elements
                            long[] flatInputMatrix = new long[inputMatrixDimensions.Length];
                            Buffer.BlockCopy(inputMatrixDimensions, 0, flatInputMatrix, 0, (int)inputMatrixSize);
                            accessor.WriteArray(writeOffset, flatInputMatrix, 0, flatInputMatrix.Length);
                            writeOffset += inputMatrixSize;

                            // Flatten and write multi-dimensional output matrix elements
                            long[] flatOutputMatrix = new long[outputMatrixDimensions.Length];
                            Buffer.BlockCopy(outputMatrixDimensions, 0, flatOutputMatrix, 0, (int)outputMatrixSize);
                            accessor.WriteArray(writeOffset, flatOutputMatrix, 0, flatOutputMatrix.Length);
                        }
                    }
                    using(FileStream fs = new FileStream(Path.Join(connectionDirectory, "Flag"), FileMode.Create, FileAccess.ReadWrite, FileShare.ReadWrite)) {
                        fs.SetLength(sizeof(byte));
                        using (MemoryMappedFile mmf = MemoryMappedFile.CreateFromFile(
                            fs, 
                            mapName: null, 
                            capacity: sizeof(byte), 
                            MemoryMappedFileAccess.ReadWrite, 
                            HandleInheritability.None, 
                            leaveOpen: false))
                        using (MemoryMappedViewAccessor accessor = mmf.CreateViewAccessor(0, sizeof(byte), MemoryMappedFileAccess.Write))
                        {
                            accessor.Write(0, (byte) 2);
                        }

                    }
                    using(FileStream fs = File.Create(Path.Join(connectionDirectory, "Input"))) {}
                    using(FileStream fs = File.Create(Path.Join(connectionDirectory, "Output"))) {}

                    buffer = Encoding.UTF8.GetBytes("{\"status\" : \"success\"}");
                }

                using var output = response.OutputStream;
                await output.WriteAsync(buffer, 0, buffer.Length);
                output.Close();
            }

            if (path == "/delete-channel") {
                byte[] buffer;
                if (string.IsNullOrEmpty(payload)) {
                    buffer = Encoding.UTF8.GetBytes("{\"status\" : \"error\"}");
                } else {
                    DeleteConnection deleteConnection = JsonSerializer.Deserialize<DeleteConnection>(payload);
                    string name = deleteConnection.name;
                    string connectionDirectory = Path.Join(directory, name);

                    if (Directory.Exists(connectionDirectory))
                        Directory.Delete(connectionDirectory, recursive: true);
                    
                    buffer = Encoding.UTF8.GetBytes("{\"status\" : \"success\"}");
                }

                using var output = response.OutputStream;
                await output.WriteAsync(buffer, 0, buffer.Length);
                output.Close();
            }
        }

        if (path == "/end-server") {
            byte[] buffer = Encoding.UTF8.GetBytes("{\"status\" : \"success\"}");

            using var output = response.OutputStream;
            await output.WriteAsync(buffer, 0, buffer.Length);
            output.Close();

            break;
        }
    }

    listener.Close();
|

static async func<void> Main() {
    task server = Server();
    Task.WaitAll(server);
}