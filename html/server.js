/**
 * server.js
 * ---------
 * Einfacher lokaler HTTP-Server, der die Ausstellungs-App
 * über http://localhost:3000 ausliefert.
 *
 * Warum wird ein Server benötigt?
 * Der Browser erlaubt setSinkId() (= Ton auf bestimmtes Audiogerät routen)
 * nur in einem "Secure Context" – das bedeutet HTTPS oder localhost.
 * Wenn die index.html direkt per file:// geöffnet wird, ist setSinkId()
 * blockiert und die Gerätezuweisung für Station 1–4 funktioniert nicht.
 *
 * Starten:
 *   node server.js
 *   npm run server
 *
 * Dann im Browser öffnen:
 *   http://localhost:3000
 */

const http = require("http");
const fs = require("fs");
const path = require("path");

const PORT = 3000;

// MIME-Typen für alle benötigten Dateiformate
const MIME_TYPES = {
  ".html": "text/html; charset=utf-8",
  ".css":  "text/css",
  ".js":   "application/javascript",
  ".mp3":  "audio/mpeg",
  ".wav":  "audio/wav",
  ".m4a":  "audio/mp4",
  ".ogg":  "audio/ogg",
  ".png":  "image/png",
  ".jpg":  "image/jpeg",
  ".mp4":  "video/mp4",
  ".ico":  "image/x-icon",
};

// ---------------------------------------------------------------------------
// Event-Kanal Admin-Screen -> iPad (Server-Sent Events)
// Der Admin-Screen POSTet Spiel-Events an /event, alle verbundenen iPads
// bekommen sie sofort ueber den offenen /events-Stream zugestellt.
// ---------------------------------------------------------------------------
const sseClients = new Set();

function broadcastEvent(jsonString) {
  for (const client of sseClients) {
    client.write(`data: ${jsonString}\n\n`);
  }
}

const server = http.createServer((req, res) => {
  // iPad meldet sich hier an und haelt die Verbindung offen
  if (req.url === "/events") {
    res.writeHead(200, {
      "Content-Type": "text/event-stream",
      "Cache-Control": "no-cache",
      "Connection": "keep-alive",
    });
    res.write("data: {\"type\":\"connected\"}\n\n");
    sseClients.add(res);
    req.on("close", () => sseClients.delete(res));
    return;
  }

  // Admin-Screen schickt hier Spiel-Events hin
  if (req.url === "/event" && req.method === "POST") {
    let body = "";
    req.on("data", (chunk) => (body += chunk));
    req.on("end", () => {
      try {
        JSON.parse(body); // nur validieren
        broadcastEvent(body);
        res.writeHead(200, { "Content-Type": "application/json" });
        res.end('{"ok":true}');
      } catch {
        res.writeHead(400);
        res.end("Ungueltiges JSON");
      }
    });
    return;
  }

  // URL dekodieren (z. B. Leerzeichen in Dateinamen)
  let urlPath = decodeURIComponent(req.url);

  // Standardseite: index.html
  if (urlPath === "/" || urlPath === "") {
    urlPath = "/index.html";
  }

  // Absoluten Dateipfad bestimmen (relativ zum Projektordner)
  const filePath = path.join(__dirname, urlPath);

  // Sicherheitscheck: Pfad muss innerhalb des Projektordners bleiben
  if (!filePath.startsWith(__dirname)) {
    res.writeHead(403);
    res.end("Forbidden");
    return;
  }

  // Datei einlesen und ausliefern
  fs.readFile(filePath, (err, data) => {
    if (err) {
      if (err.code === "ENOENT") {
        res.writeHead(404, { "Content-Type": "text/plain" });
        res.end("Datei nicht gefunden: " + urlPath);
      } else {
        res.writeHead(500, { "Content-Type": "text/plain" });
        res.end("Server-Fehler: " + err.message);
      }
      return;
    }

    const ext = path.extname(filePath).toLowerCase();
    const contentType = MIME_TYPES[ext] || "application/octet-stream";

    res.writeHead(200, { "Content-Type": contentType });
    res.end(data);
  });
});

server.listen(PORT, () => {
  console.log("=================================================");
  console.log(`  Server läuft: http://localhost:${PORT}`);
  console.log("  Im Browser öffnen und dann Geräte auswählen.");
  console.log("  Beenden: Strg+C");
  console.log("=================================================");
});
