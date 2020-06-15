Import("env")

def parse_line(line):
  name, val = line.split('=', 1)
  val = val.strip().replace('"', '\\"')
  return (name.strip(), val.strip())

file = open('./.env', 'r')
lines = [line.rstrip('\n') for line in file]
variables = map(parse_line, lines)
env.Append(CPPDEFINES=variables)
