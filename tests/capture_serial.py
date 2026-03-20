#!/usr/bin/env python3
import subprocess
import sys
import time

import serial
import serial.serialutil


def open_serial_until_deadline(port: str, baud: int, deadline: float):
    while time.time() < deadline:
        try:
            ser = serial.Serial(port, baud, timeout=0.2, dsrdtr=False, rtscts=False)
            ser.dtr = False
            ser.rts = False
            return ser
        except (OSError, serial.serialutil.SerialException):
            time.sleep(0.05)
    return None


def force_reset_via_esptool(port: str, baud: int) -> None:
    try:
        subprocess.run(
            [
                sys.executable,
                "-m",
                "esptool",
                "--chip",
                "esp32c3",
                "--port",
                port,
                "--baud",
                str(baud),
                "chip_id",
            ],
            check=False,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            timeout=5.0,
        )
    except Exception:
        pass


def main() -> int:
    if len(sys.argv) != 4:
        print("usage: capture_serial.py <port> <baud> <seconds>", file=sys.stderr)
        return 2

    port = sys.argv[1]
    baud = int(sys.argv[2])
    seconds = float(sys.argv[3])

    chunks = []
    deadline = time.time() + max(seconds, 1.0)
    reset_pulsed = False
    esptool_reset_attempted = False

    while time.time() < deadline:
        ser = open_serial_until_deadline(port, baud, deadline)
        if ser is None:
            break

        try:
            first_data_deadline = min(deadline, time.time() + 0.75)
            while time.time() < deadline:
                try:
                    data = ser.read(4096)
                except (OSError, serial.serialutil.SerialException):
                    break
                if data:
                    chunks.append(data)
                    first_data_deadline = deadline
                    continue
                if (not reset_pulsed) and (time.time() >= first_data_deadline):
                    try:
                        ser.rts = True
                        time.sleep(0.10)
                        ser.rts = False
                        reset_pulsed = True
                        first_data_deadline = min(deadline, time.time() + 0.75)
                    except (OSError, serial.serialutil.SerialException):
                        break
                elif (not esptool_reset_attempted) and (time.time() >= first_data_deadline):
                    try:
                        ser.close()
                    except Exception:
                        pass
                    force_reset_via_esptool(port, baud)
                    esptool_reset_attempted = True
                    break
        finally:
            try:
                ser.close()
            except Exception:
                pass

    sys.stdout.buffer.write(b"".join(chunks))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
