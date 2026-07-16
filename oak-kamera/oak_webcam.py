"""
oak_webcam.py
-------------
Bootet die OAK-D Lite und stellt sie als normale UVC-Webcam bereit.

Hintergrund: Die OAK-D Lite ist KEINE normale Webcam. Sie hat keinen eigenen
Speicher und meldet sich am USB nur als "Movidius MyriadX". Erst wenn dieses
Script laeuft, bekommt sie ihre Firmware und erscheint im System (und damit im
Browser / Admin-Screen) als Kamera mit "Luxonis"-Label - genau das, wonach
index.html sucht (OAK_CAMERA_LABEL_PATTERN).

WICHTIG: Dieses Script muss die ganze Zeit laufen, solange die Kamera
gebraucht wird. Fenster schliessen = Kamera weg.

Starten:  Doppelklick auf "Kamera starten.command"
oder:     ./oak-kamera/venv/bin/python oak-kamera/oak_webcam.py
"""

import sys
import time

import depthai as dai

WIDTH, HEIGHT, FPS = 1920, 1080, 30
RETRY_DELAY_S = 5


def build_pipeline() -> dai.Pipeline:
    # UVC muss VOR dem Boot in der Geraete-Config angekuendigt werden,
    # damit die Firmware den USB-Webcam-Deskriptor einrichtet.
    config = dai.Device.Config()
    config.board.uvc = dai.BoardConfig.UVC(WIDTH, HEIGHT)
    config.board.uvc.frameType = dai.ImgFrame.Type.NV12
    device = dai.Device(config)

    pipeline = dai.Pipeline(device)
    cam = pipeline.create(dai.node.Camera).build(dai.CameraBoardSocket.CAM_A)
    uvc = pipeline.create(dai.node.UVC)
    # UVC (Webcam-Modus) erwartet NV12-Frames
    cam.requestOutput((WIDTH, HEIGHT), dai.ImgFrame.Type.NV12, fps=FPS).link(uvc.input)
    return pipeline


def run_once() -> None:
    pipeline = build_pipeline()
    pipeline.start()
    print("=" * 55)
    print("  OAK-D Lite laeuft jetzt als Webcam (UVC-Modus).")
    print(f"  Aufloesung: {WIDTH}x{HEIGHT} @ {FPS} fps")
    print("  Die Kamera erscheint im Browser als 'Luxonis'-Geraet.")
    print("  Dieses Fenster GEOEFFNET lassen! Beenden: Strg+C")
    print("=" * 55)
    while pipeline.isRunning():
        time.sleep(1)
    print("Pipeline wurde beendet (Kamera getrennt?).")


def main() -> None:
    while True:
        try:
            run_once()
        except KeyboardInterrupt:
            print("\nBeendet.")
            sys.exit(0)
        except Exception as exc:  # Boot-/Verbindungsfehler -> erneut versuchen
            print(f"[FEHLER] {exc}")
            print("Tipp: Kamera DIREKT am MacBook anschliessen (kein Hub/Dock),")
            print("      USB-3-Kabel verwenden.")
        print(f"Neuer Versuch in {RETRY_DELAY_S} s ... (Abbrechen: Strg+C)")
        try:
            time.sleep(RETRY_DELAY_S)
        except KeyboardInterrupt:
            print("\nBeendet.")
            sys.exit(0)


if __name__ == "__main__":
    main()
