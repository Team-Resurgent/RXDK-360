// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.

using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using System.Text.Json;
using System.Threading;

namespace Rxdk.Xbox360.DebugAdapter
{
    /// <summary>
    /// Debug Adapter Protocol transport: Content-Length framed JSON over a pair of
    /// streams (stdin/stdout when the editor launches the adapter). Requests come in,
    /// responses and events go out; the session (DebugSession) supplies the handlers.
    /// </summary>
    public sealed class DapConnection
    {
        private readonly Stream _in, _out;
        private readonly object _writeLock = new();
        private int _seq;

        public DapConnection(Stream input, Stream output) { _in = input; _out = output; }

        /// <summary>A parsed incoming message (request), as raw JSON.</summary>
        public sealed class Message
        {
            public int Seq;
            public string Type = "";
            public string Command = "";
            public JsonElement Arguments;
            public bool HasArguments;
        }

        public Message? Read()
        {
            int length = ReadHeaderLength();
            if (length < 0) return null;
            var buf = new byte[length];
            int got = 0;
            while (got < length)
            {
                int n = _in.Read(buf, got, length - got);
                if (n <= 0) return null;
                got += n;
            }
            using var doc = JsonDocument.Parse(buf);
            var root = doc.RootElement;
            var m = new Message
            {
                Seq = root.TryGetProperty("seq", out var s) ? s.GetInt32() : 0,
                Type = root.TryGetProperty("type", out var t) ? t.GetString() ?? "" : "",
                Command = root.TryGetProperty("command", out var c) ? c.GetString() ?? "" : "",
            };
            if (root.TryGetProperty("arguments", out var a))
            {
                m.Arguments = a.Clone();
                m.HasArguments = true;
            }
            return m;
        }

        private int ReadHeaderLength()
        {
            // Read the "Content-Length: N\r\n\r\n" header.
            var line = new StringBuilder();
            int length = -1;
            while (true)
            {
                int b = _in.ReadByte();
                if (b < 0) return length >= 0 ? length : -1;
                if (b == '\r') { _in.ReadByte(); /* \n */
                    string l = line.ToString();
                    line.Clear();
                    if (l.Length == 0) return length;                 // blank line ends the header
                    const string k = "Content-Length:";
                    if (l.StartsWith(k, StringComparison.OrdinalIgnoreCase))
                        int.TryParse(l.AsSpan(k.Length).Trim(), out length);
                }
                else line.Append((char)b);
            }
        }

        public void SendResponse(Message request, bool success, object? body = null, string? message = null)
        {
            var resp = new Dictionary<string, object?>
            {
                ["seq"] = Interlocked.Increment(ref _seq),
                ["type"] = "response",
                ["request_seq"] = request.Seq,
                ["success"] = success,
                ["command"] = request.Command,
            };
            if (message != null) resp["message"] = message;
            if (body != null) resp["body"] = body;
            Write(resp);
        }

        public void SendEvent(string eventName, object? body = null)
        {
            var evt = new Dictionary<string, object?>
            {
                ["seq"] = Interlocked.Increment(ref _seq),
                ["type"] = "event",
                ["event"] = eventName,
            };
            if (body != null) evt["body"] = body;
            Write(evt);
        }

        private static readonly JsonSerializerOptions JsonOpts = new() { IncludeFields = true };

        private void Write(object payload)
        {
            byte[] json = JsonSerializer.SerializeToUtf8Bytes(payload, JsonOpts);
            byte[] header = Encoding.ASCII.GetBytes($"Content-Length: {json.Length}\r\n\r\n");
            lock (_writeLock)
            {
                _out.Write(header, 0, header.Length);
                _out.Write(json, 0, json.Length);
                _out.Flush();
            }
        }
    }
}
