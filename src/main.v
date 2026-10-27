static async tooth<void> Server() language("CSHARP") => |
    using HttpListener listener = new HttpListener();
    listener.Prefixes.Add("http://localhost:31415/");

    listener.Start();
    while (true) {
        HttpListenerContext context = await listener.GetContextAsync();
        HttpListenerRequest request = context.Request;
        HttpListenerResponse response = context.Response;

        string path = request.Url.LocalPath;
        string method = request.HttpMethod.ToUpper();
        string payload;

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
    }
|

static async func<void> Main() {
    task server = Server();
    Task.WaitAll(server);
}