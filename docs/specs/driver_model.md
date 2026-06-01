# Driver Model Specification

## 1. Philosophy
SageOS employs a modular and externalizable driver model. The kernel core is kept minimal, containing only essential hardware interfaces, interrupt routing, and DMA mediation. Most device logic is intended to run as isolated services.

## 2. Driver Classes
- **Kernel-Native**: High-performance drivers residing in the kernel (e.g., core timers, interrupt controllers, early UART).
- **Userspace Driver**: Native ELF64 processes running with specific hardware access capabilities.
- **SGVM-Managed**: Drivers written in SageLang, executing within the managed runtime (default for high-level logic like VFS bridges).
- **Hybrid Driver**: A native shim for low-level I/O coupled with SageLang logic for protocol handling.

## 3. Device Namespace
All hardware is exposed through a unified virtual namespace:
`/device/<class>/<instance>`

Examples:
- `/device/block/nvme0`: NVMe storage device.
- `/device/net/eth0`: Ethernet controller.
- `/device/input/kbd0`: Keyboard input.

## 4. Driver API (`driver.h`)
Drivers interact with the kernel through a standardized set of interfaces:
- `driver_register()`: Announces a new driver and its supported device classes.
- `device_event_notify()`: Pushes hardware events (e.g., keypress, packet arrival) into the system's asynchronous event queue.
- `dma_map_buffer()`: Securely maps memory regions for hardware Direct Memory Access, mediated by the kernel.

## 5. Communication & Synchronization
Drivers communicate with the rest of the system using:
- **Event Queues**: For asynchronous notifications.
- **DMA Channels**: For high-bandwidth data transfer.
- **Capability Handles**: To securely reference memory and I/O ports.
- **Async Requests**: For non-blocking command submission.

## 6. Safety & Isolation (Future)
To enhance system reliability, the driver model is designed to support:
- **Isolated Address Spaces**: Drivers running in their own virtual memory domains.
- **Fault Containment**: Preventing a driver crash from compromising the kernel.
- **Hot-Reload**: The ability to update or restart drivers without a full system reboot.
