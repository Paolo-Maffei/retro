Import("env")

def fixLinkFlag (s):
    return s[4:] if s.startswith('-Wl,-T') else s
    
env.Replace(LINKFLAGS = [fixLinkFlag(i) for i in env['LINKFLAGS']])
