/**
 * audio-player.js
 * ---------------
 * Spielt 4 Audiodateien gleichzeitig auf 2 Audiogeräten ab.
 * Jede Station bekommt entweder den linken oder rechten Kanal
 * des jeweiligen Geräts – so klingen 4 Stationen räumlich getrennt.
 *
 * Gerätezuweisung:
 *   Station 1 → USB-Lautsprecher, LINKS
 *   Station 2 → USB-Lautsprecher, RECHTS
 *   Station 3 → Kopfhörerausgang, LINKS
 *   Station 4 → Kopfhörerausgang, RECHTS
 *
 * Voraussetzung: mpv muss installiert sein.
 *   macOS: brew install mpv
 *
 * Verfügbare Audio-Devices anzeigen:
 *   mpv --audio-device=help
 *
 * Starten:
 *   node audio-player.js
 *   npm start
 */

const { spawn } = require("child_process");
const fs = require("fs");
const path = require("path");

// ---------------------------------------------------------------------------
// KONFIGURATION – hier die echten Gerätenamen eintragen
// ---------------------------------------------------------------------------
// Die Gerätenamen bekommst du mit folgendem Terminal-Befehl heraus:
//   mpv --audio-device=help
//
// Beispiele für macOS (CoreAudio):
//   "coreaudio/UID des Geräts"  →  z. B. "coreaudio/AppleUSBAudioEngine:..."
//   Den exakten String bitte aus der mpv-Ausgabe kopieren.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Audiodateien und Gerätezuordnung pro Station
// ---------------------------------------------------------------------------
// channel: 'left'  → nur linker  Lautsprecher des Geräts
// channel: 'right' → nur rechter Lautsprecher des Geräts
// ---------------------------------------------------------------------------

const sounds = [
  {
    // Station 1 – USB-Lautsprecher, linker Kanal
    file: "Sounds/Station 1/Aha V1.mp3",
    device: "coreaudio/AppleUSBAudioEngine:C-Media Electronics Inc.:USB Audio Device:3100000:2,1",
    channel: "left",
    volume: 100
  },
  {
    // Station 2 – USB-Lautsprecher, rechter Kanal
    file: "Sounds/Station 1/Huhu V1.mp3",
    device: "coreaudio/AppleUSBAudioEngine:C-Media Electronics Inc.:USB Audio Device:3100000:2,1",
    channel: "right",
    volume: 100
  },
  {
    // Station 3 – Kopfhörerausgang, linker Kanal
    file: "Sounds/Station 2/footsteps_walking_in.mp3",
    device: "avfoundation/BuiltInHeadphoneOutputDevice",
    channel: "left",
    volume: 100
  },
  {
    // Station 4 – Kopfhörerausgang, rechter Kanal
    file: "Sounds/Station 2/footsteps _walking_out.mp3",
    device: "avfoundation/BuiltInHeadphoneOutputDevice",
    channel: "right",
    volume: 100
  }
];

// ---------------------------------------------------------------------------
// FUNKTION: playOnDevice
// ---------------------------------------------------------------------------
/**
 * Spielt eine Audiodatei auf einem bestimmten Audio-Device ab.
 *
 * @param {string} filePath    - Pfad zur Audiodatei (relativ zum Projektordner)
 * @param {string} audioDevice - Name des Audio-Devices (aus `mpv --audio-device=help`)
 * @param {number} volume      - Lautstärke 0–130 (über 100 = Verstärkung)
 * @param {string} channel     - 'left', 'right' oder 'stereo' (Standard)
 */
function playOnDevice(filePath, audioDevice, volume = 100, channel = "stereo") {
  // Absoluten Pfad zur Audiodatei bestimmen
  const absolutePath = path.resolve(__dirname, filePath);

  // Prüfen, ob die Datei existiert
  if (!fs.existsSync(absolutePath)) {
    console.error(`[FEHLER] Audiodatei nicht gefunden: ${absolutePath}`);
    return;
  }

  console.log(`[START] "${filePath}" → Device: "${audioDevice}" (${channel})`);

  // Pan-Filter bestimmen:
  // 'left'  → nur linker  Kanal, rechter Kanal stumm
  // 'right' → nur rechter Kanal, linker  Kanal stumm
  // 'stereo'→ normales Stereo (kein Filter)
  const panArgs = [];
  if (channel === "left") {
    panArgs.push("--af=pan=stereo|c0=c0|c1=0");
  } else if (channel === "right") {
    panArgs.push("--af=pan=stereo|c0=0|c1=c0");
  }

  // mpv-Prozess starten
  // --no-video        → kein Videofenster öffnen
  // --audio-device    → Ausgabe auf das gewünschte Gerät leiten
  // --af=pan=...      → L/R-Kanal-Routing (nur wenn channel !== 'stereo')
  const mpvProcess = spawn("mpv", [
    "--no-video",
    `--audio-device=${audioDevice}`,
    `--volume=${volume}`,
    ...panArgs,
    absolutePath
  ]);

  // Ausgabe von mpv im Terminal anzeigen (optional, zum Debuggen)
  mpvProcess.stdout.on("data", (data) => {
    process.stdout.write(`[mpv | ${path.basename(filePath)}] ${data}`);
  });

  mpvProcess.stderr.on("data", (data) => {
    process.stderr.write(`[mpv | ${path.basename(filePath)}] ${data}`);
  });

  // Meldung wenn der Sound fertig abgespielt wurde
  mpvProcess.on("close", (code) => {
    if (code === 0) {
      console.log(`[ENDE]  "${filePath}" wurde vollständig abgespielt.`);
    } else {
      console.error(
        `[FEHLER] "${filePath}" beendet mit Exit-Code ${code}. ` +
        `Prüfe, ob das Audio-Device "${audioDevice}" korrekt ist.`
      );
    }
  });

  // Fehler beim Starten von mpv abfangen (z. B. mpv nicht installiert)
  mpvProcess.on("error", (err) => {
    console.error(
      `[FEHLER] mpv konnte nicht gestartet werden: ${err.message}\n` +
      `→ Ist mpv installiert? (brew install mpv)`
    );
  });
}

// ---------------------------------------------------------------------------
// AUDIO DEAKTIVIERT
// ---------------------------------------------------------------------------
// Die Sound-Logik wurde deaktiviert. Alle Audiofunktionen sind stummgeschaltet.
console.log('[AUDIO DEAKTIVIERT] audio-player.js wurde deaktiviert.');
console.log('[AUDIO DEAKTIVIERT] playOnDevice() wird nicht aufgerufen.');
console.log('[AUDIO DEAKTIVIERT] Keine Sounds werden abgespielt.');

// ORIGINAL CODE (DEAKTIVIERT):
// sounds.forEach(({ file, device, volume, channel }) => {
//   playOnDevice(file, device, volume, channel);
// });
