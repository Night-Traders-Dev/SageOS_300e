# test_dict.sage
let d = {}
d["key"] = "value"
d["num"] = 42
print("Dictionary test:")
print(d["key"])
print(d["num"])
d["key"] = "updated"
print(d["key"])
if d["num"] == 42:
    print("Condition check passed")
end
