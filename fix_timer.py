import re

with open('sageos_build/kernel/core/sagelang/timer.sage', 'r') as f:
    content = f.read()

# Replace 'fn name() {' with 'proc name():' and '}' with 'end'
# This is a basic regex, need to be careful
content = re.sub(r'fn\s+(\w+)\s*\(\)\s*\{', r'proc \1():', content)
content = content.replace('}', 'end')

# Fix a known issue where replaced '}' might be at end of block
# The manual fix will be tedious, so I'll write the whole file content out 
# with the correct syntax for the specific functions.
