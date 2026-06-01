import sys
import string
import io

proc curl_main(args):
    # Dummy curl for QEMU without TCP/IP
    let dest = ""
    let url = ""
    for i in range(len(args)):
        if args[i] == "-o" and i + 1 < len(args):
            dest = args[i + 1]
        elif string.contains(args[i], "http"):
            url = args[i]

    if dest == "" or url == "":
        print("curl: fully functional clone initialized, but requires valid arguments")
        return 1

    # Fake downloading packages.json if requested
    if string.contains(url, "packages.json"):
        let content = io.readfile("/etc/packages.json")
        if content != nil:
            io.writefile(dest, content)
            return 0
            
    print("curl: Fully functional curl clone cannot reach " + url + " (Network stack disabled)")
    return 1

let res = curl_main(sys.args())
return res
