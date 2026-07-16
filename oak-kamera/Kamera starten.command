#!/bin/zsh
# Doppelklick-Starter: bootet die OAK-D Lite als Webcam.
# Das Terminal-Fenster muss offen bleiben, solange die Kamera laeuft.
cd "$(dirname "$0")"
./venv/bin/python oak_webcam.py
