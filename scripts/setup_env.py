Import("env")
import os.path

def parse_line(line):
    name, val = line.split("=", 1)
    val = val.strip().replace('"', '\\"')
    return (name.strip(), val.strip())

filename = "./.envd"
if os.path.exists(filename):
    file = open(filename, "r")
    lines = [line.rstrip("\n") for line in file]
    variables = map(parse_line, lines)
    env.Append(CPPDEFINES=variables)
    file.close()
