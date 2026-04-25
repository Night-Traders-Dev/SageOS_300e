gc_disable()

# User shell entry point.
# The implementation lives in os.kernel.shell while the kernel is being
# ported to pure SageLang, keeping kernel and user shell behavior aligned.

import os.kernel.shell as kernel_shell

proc sh_main():
    kernel_shell.sh_main()
end

sh_main()
