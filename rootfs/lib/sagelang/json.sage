# json.sage — Procedural JSON parser for MetalVM compatibility
import strings

# cJSON compatibility interface
proc cJSON_Parse(text):
    let res = parse(text)
    return res

proc cJSON_ToSage(val):
    return val

proc cJSON_Delete(val):
    return nil

proc cJSON_Print(val):
    return stringify(val)

proc cJSON_FromSage(val):
    return val

# Core parser implementation
proc parse(text):
    if text == nil: return nil
    let state = {"text": text, "pos": 0}
    _skip_whitespace(state)
    return _parse_value(state)

proc _skip_whitespace(state):
    let text = state["text"]
    while state["pos"] < len(text):
        let c = text[state["pos"]]
        if c == " " or c == chr(10) or c == chr(13) or c == chr(9):
            state["pos"] = state["pos"] + 1
        else:
            break

proc _parse_value(state):
    _skip_whitespace(state)
    if state["pos"] >= len(state["text"]): return nil
    let c = state["text"][state["pos"]]
    if c == "{": return _parse_object(state)
    if c == "[": return _parse_array(state)
    if c == chr(34): return _parse_string(state)
    if c == "t": return _parse_literal(state, "true", true)
    if c == "f": return _parse_literal(state, "false", false)
    if c == "n": return _parse_literal(state, "null", nil)
    return _parse_number(state)

proc _parse_literal(state, lit, val):
    state["pos"] = state["pos"] + len(lit)
    return val

proc _parse_string(state):
    state["pos"] = state["pos"] + 1 # skip "
    let res = ""
    let text = state["text"]
    while state["pos"] < len(text):
        let c = text[state["pos"]]
        if c == chr(34):
            state["pos"] = state["pos"] + 1
            return res
        if c == chr(92): # backslash
            state["pos"] = state["pos"] + 1
            let esc = text[state["pos"]]
            if esc == "n": res = res + chr(10)
            elif esc == "r": res = res + chr(13)
            elif esc == "t": res = res + chr(9)
            elif esc == chr(34): res = res + chr(34)
            elif esc == chr(92): res = res + chr(92)
            else: res = res + esc
        else:
            res = res + c
        state["pos"] = state["pos"] + 1
    return res

proc _parse_number(state):
    let start = state["pos"]
    let text = state["text"]
    while state["pos"] < len(text):
        let c = text[state["pos"]]
        if (c >= "0" and c <= "9") or c == "." or c == "-":
            state["pos"] = state["pos"] + 1
        else:
            break
    let s = strings.string_substr(text, start, state["pos"] - start)
    return strtod(s, nil)

proc _parse_array(state):
    state["pos"] = state["pos"] + 1 # skip [
    let arr = []
    _skip_whitespace(state)
    if state["text"][state["pos"]] == "]":
        state["pos"] = state["pos"] + 1
        return arr
    while true:
        push(arr, _parse_value(state))
        _skip_whitespace(state)
        let c = state["text"][state["pos"]]
        if c == ",":
            state["pos"] = state["pos"] + 1
            _skip_whitespace(state)
        elif c == "]":
            state["pos"] = state["pos"] + 1
            return arr
        else:
            break
    return arr

proc _parse_object(state):
    state["pos"] = state["pos"] + 1 # skip {
    let obj = {}
    _skip_whitespace(state)
    if state["text"][state["pos"]] == "}":
        state["pos"] = state["pos"] + 1
        return obj
    while true:
        let key = _parse_string(state)
        _skip_whitespace(state)
        if state["text"][state["pos"]] == ":":
            state["pos"] = state["pos"] + 1
        _skip_whitespace(state)
        obj[key] = _parse_value(state)
        _skip_whitespace(state)
        let c = state["text"][state["pos"]]
        if c == ",":
            state["pos"] = state["pos"] + 1
            _skip_whitespace(state)
        elif c == "}":
            state["pos"] = state["pos"] + 1
            return obj
        else:
            break
    return obj

proc stringify(val):
    let t = type(val)
    if val == nil: return "null"
    if t == "bool":
        if val: return "true"
        return "false"
    if t == "number": return str(val)
    if t == "string": return chr(34) + val + chr(34)
    if t == "array":
        let res = "["
        for i in range(len(val)):
            if i > 0: res = res + ","
            res = res + stringify(val[i])
        return res + "]"
    if t == "dict":
        let res = "{"
        let keys = dict_keys(val)
        for i in range(len(keys)):
            if i > 0: res = res + ","
            res = res + chr(34) + keys[i] + chr(34) + ":" + stringify(val[keys[i]])
        return res + "}"
    return "null"
