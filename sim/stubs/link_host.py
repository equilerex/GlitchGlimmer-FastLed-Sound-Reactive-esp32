Import("env")
import sys

if sys.platform == "win32":
    env.Append(LIBS=["ws2_32"])
