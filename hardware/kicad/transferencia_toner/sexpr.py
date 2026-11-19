def tokenize(s):
    i, n = 0, len(s)
    while i < n:
        c = s[i]
        if c in '()':
            yield c; i += 1
        elif c.isspace():
            i += 1
        elif c == '"':
            j = i + 1
            while s[j] != '"':
                j += 2 if s[j] == '\\' else 1
            yield s[i:j + 1]; i = j + 1
        else:
            j = i
            while j < n and not s[j].isspace() and s[j] not in '()':
                j += 1
            yield s[i:j]; i = j

def parse(s):
    stack = [[]]
    for t in tokenize(s):
        if t == '(':
            stack.append([])
        elif t == ')':
            x = stack.pop(); stack[-1].append(x)
        else:
            stack[-1].append(t)
    return stack[0][0]

def dump(x, ind=0):
    if not isinstance(x, list):
        return x
    if all(not isinstance(e, list) for e in x):
        return '(' + ' '.join(x) + ')'
    out = '(' + ' '.join(e for e in x if not isinstance(e, list) and x.index(e) == 0) if False else '('
    head = []
    i = 0
    while i < len(x) and not isinstance(x[i], list):
        head.append(x[i]); i += 1
    out = '(' + ' '.join(head)
    for e in x[i:]:
        out += '\n' + '\t' * (ind + 1) + (dump(e, ind + 1) if isinstance(e, list) else e)
    return out + '\n' + '\t' * ind + ')'

def num(v):
    s = ('%.6f' % v).rstrip('0').rstrip('.')
    return '0' if s in ('-0', '') else s

def find(node, key):
    return [e for e in node if isinstance(e, list) and e and e[0] == key]
