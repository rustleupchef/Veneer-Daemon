class CreateConnection {
    public string name { get; private set; }
    public string[] inputs { get; private set; }
    public string[] outputs { get; private set;}

    public CreateConnection(string name, string[] inputs, string[] outputs) {
        this.name = name;
        this.inputs = inputs;
        this.outputs = outputs;
    }
}

class DeleteConnection {
    public string name { get; private set; }

    public DeleteConnection(string name) {
        this.name = name;
    }
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
                    using(FileStream fs = File.Create(Path.Join(connectionDirectory, "Schema"))) {}
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
    }
|

static async func<void> Main() {
    task server = Server();
    Task.WaitAll(server);
}