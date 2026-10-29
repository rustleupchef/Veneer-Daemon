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

                    using(FileStream fs = new FileStream(Path.Join(connectionDirectory, "Schema"), FileMode.Create, FileAccess.ReadWrite, FileShare.ReadWrite)) {
                        byte[] inputBytes = ConvertTypesToBytes(createConnection.inputs);
                        long[,] inputMatrixDimensions = ConvertToMatrix(createConnection.inputDimensions);

                        byte[] outputBytes = ConvertTypesToBytes(createConnection.outputs);
                        long[,] outputMatrixDimensions = ConvertToMatrix(createConnection.outputDimensions);

                        long inputByteSize = inputBytes.Length;
                        long outputByteSize = outputBytes.Length;

                        long inputMatrixSize = inputMatrixDimensions.Length * sizeof(long);
                        long outputMatrixSize = outputMatrixDimensions.Length * sizeof(long);

                        long totalSize = (inputByteSize + outputByteSize) + (inputMatrixSize + outputMatrixSize);
                        fs.SetLength(totalSize);
                    }
                    using(FileStream fs = File.Create(Path.Join(connectionDirectory, "Input"))) {}
                    using(FileStream fs = File.Create(Path.Join(connectionDirectory, "Output"))) {}
                    using(FileStream fs = File.Create(Path.Join(connectionDirectory, "Flag"))) {}

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