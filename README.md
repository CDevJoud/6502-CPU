# Writing Hello World on a 6502 emulated CPU in C

## Motivation  

We all have started programming with high level programming languages at some point in our life, like `Python` and our first program was the famous phrase `Hello World`.
This was our begining of our journey and we later dived deeper in the programming ice berg from `Python/Javascript` -> `C#/Java` -> `C/C++` and later we discovered that `C` 
and `Assembly` was the farthest we can get in term of low level programming... or is it?

The purpose of this article is to demonstrate a new perspective of programming and it's by emulating a real CPU or by creating custom architected CPUs with custom instructions set and develop
higher level of abstractions to create a fine grained applications or systems with the consideration of covering fundemental aspects of CPUs such as `Processor Status` formerly known 
as `flags`, `Program Counter`, `Stack Pointer`, `Accumulator` and `Registers`.

MOS 6502 CPU emulation can be really interesting, as it helps in understanding low-level programming languages such as `C` and `Assembly`. It also shows how pointers work under the hood.
I believe that learning and building a simple MOS 6502 emulator is a good first step into Assembly, since it introduces the primitive instructions that can later help in creating higher
levels of abstraction, and give a better perspective on how modern CPUs actually work.

## Goals Of The Project

The goal of this project is to keep the implementation as simple as possible, while still allowing it to be expandable later by the reader.
Instead of building a complete and complex emulator from the start, the focus here is on creating a minimal and understandable foundation.
For the sake of simplicity, we will demonstrate how to write the phrase `Hello World` into the MOS 6502 memory. This allows us to focus on the core idea of how the CPU interacts with memory
without introducing too many concepts at once.
Later this data will be captured from memory and printed to the standard output. This approach helps visualize how output can be handled indirectly through memory rather than printing 
directly, which is closer to how real systems operate.

## CPU Mental model
In this simplified model, the CPU is a small system that updates its internal state step by step (instruction by instruction). It does not understand programs in a high level way, but
simply [fetch](https://en.wikipedia.org/wiki/Instruction_cycle#Fetch_stage) instructions from memory and executes them.
The CPU is defined by its registers, flags and program counter, which keep track of data and the current position in the program.
Each instruction modifies this state, such as changing a register value or updating the program counter. Over time, these small changes result in the execution of a program.
### Program Counter 
The program counter is a 16-bit register that points to the next instruction to be executed. It's value is automatically updated as instructions are processed.
However, it can also be changed manually by certain instructions, such as jumps, branches, or subroutine calls, which redirect execution to another memory location. It's also updated when
returning from a [subroutine](https://en.wikipedia.org/wiki/Function_(computer_programming)#) or an [interrupt](https://en.wikipedia.org/wiki/Interrupt)
### Stack Pointer
The processor uses 256-byte stack located between `0x0100` and `0x01FF`. The stack pointer is an 8-bit register that holds low 8 bits of the next free position on the stack. The stack 
location is fixed and cannot be changed.
When a byte is pushed onto the stack, the stack pointer is decremented. When a byte is pulled from the stack, it's incremented.
The CPU doesn't check for stack overflow or underflow. As a result, pushing or pulling to often may overwrite memory and can cause the program to crash or stuck in endless loop.
### Accumulator
The 8-bit accumulator is used for most arithmetic and logical operations, with the exceptions of increment and decrement instructions. It's contents can be stored in or retrieved from memory
or the stack.
Most complex operations rely on the accumulator for calculations, so using it efficiently and wisely is important, especially in time critical routines.
### Registers X and Y
The 8 bit index register is most commonly used to hold counters or offsets for accessing memory. The value of the X or Y registers can be loaded and saved in memory, compared with values 
held in memory or incremented and decremented.
### Processor Status
As instructions are executed, a set of processor flags are either set or cleared to record the result of each operation. These flags, along with some control flags, are stored in a special
status register, where each flag takes a single bit.
* `Carry`: Is set if the last operation produces a carry out of bit 7 or a borrow from bit 0 for ex `0b11111111 + 0b00000001` -> Cary Flag and Zero Flag is set (reason the result is zero)
* `Zero`: The zero flag is set if the result of the last operation was zero.
* `Interupt Disable`: The interrupt disable flag is set if the program has executed a `Set Interrupt Disable` (SEI) instruction or `Clear Interrupt Disable` (CLI) instruction, for this article this flag will be ignored.
* `Decimal`: Decimal mode flag is set the processor will obey the rules of Binary Coded Decimal (BCD) arithmetic during addition and subtraction. The flag can be explicitly set using 'Set Decimal Flag' (SED) and cleared with 'Clear Decimal Flag' (CLD). For this article this flag will be ignored.
* `Break`: The break command bit is set when a BRK instruction has been executed and an interrupt has been generated to process it.
* `Overflow`: The overflow flag is set during arithmetic operations if the result has yielded an invalid Two's complement result, for example `0b01000000 + 0b01000000 = 0b10000000` -> Overflow flag is set
* `Negative`: The negative flag is set if the result of the last operation had bit 7 set to a one. for example `0b10000000` the last bit is on, it means it's a negative.

## Memory Model
In C, memory is represented as a simple array of bytes. Each element in the array acts like a memory address and it's index represent to a sepcific location in memory.
For example a `uint8_t mem[0xFFFF]` array can represent a full 64KB of addressable memory, where each index from 0x0000 to 0xFFFF stores one byte of data.

64KB is used in this model because it matches the address space of MOS 6502 processor. The 6502 has a 16 bit address bus which allows it to access 2^16 = 0xFFFF memory locations.

The CPU interacts with memory by reading and writing values to specific memory addresses. The program counter tells which address to read next for instructions, and other registers are used
to read or store data.
Instructions like load and store directly move data between registers and memory, in this way memory acts as the main storage for both program code and data, and the CPU reads from and 
writes to it during execution

---

Now enough theories... let's dive into the important part

## Definitions
We start by creating a file, i will call it m6502.c, in the begining i will write the code below:

```cpp
#define FLAG_C 0b00000001 // Carry
#define FLAG_Z 0b00000010 // Zero
#define FLAG_I 0b00000100 // Interrupt Disable
#define FLAG_D 0b00001000 // Decimal 
#define FLAG_B 0b00010000 // Break
#define FLAG_U 0b00100000 // Useless
#define FLAG_V 0b01000000 // Overflow
#define FLAG_N 0b10000000 // Negative
```

The code above defines CPU flags, every flag has an 8-bit value, and the reason why the values is assigned int that order is because we can combine multiple flags in one flag state for 
example `FLAG_C` and `FLAG_Z`, in code `FLAG_C | FLAG_Z` and we use the or `|` operator to combine them.

The second thing we need is helper macros for CPU behaviors

```cpp
#define SET_FLAG(cpu, flag, cond) (cpu->flags = (cond) ? (cpu->flags | flag) : (cpu->flags & ~flag))
#define GET_FLAG(cpu, flag) (cpu->flags & flag)
#define PAGE_CROSSED(base, addr) ((base & 0xFF00) != (addr & 0xFF00))
#define IS_ZERO(r) (r == 0)
#define IS_NEGATIVE(r) (r & 0b10000000)
#define IS_CARRY(r) (r >= 0b10000000)
```
> The `#define SET_FLAG(cpu, flag, cond) (cpu->flags = (cond) ? (cpu->flags | flag) : (cpu->flags & ~flag))` macros is used to set or clear a specific flag inside the CPU state register.
> If the condition is true, the flag bit is turned on. If the condition is false, the flag bit is turned off.

> The `#define GET_FLAG(cpu, flag) (cpu->flags & flag)` macro checks whether a specific flag is currently set.
> It returns a non zero value if the flag is active, otherwise it returns zero.

> The `#define PAGE_CROSSED(base, addr) ((base & 0xFF00) != (addr & 0xFF00))` macro checks whether 2 memory addresses are in different 256 byte pages.
> It's used to detec when an operation crosses a page boundary, which can affect CPU timing.

> The `#define IS_ZERO(r) (r == 0)` checks if a value is zero. This is commonly used to update the zero flag.

> The `#define IS_NEGATIVE(r) (r & 0b10000000)` checks if the most significant bit (bit 7) is set, which indicates a negative value in 8-bit signed representation.

> The `#define IS_CARRY(r) (r >= 0b10000000)` checks if a value has produced a carry beyond 8-bits. This is used to simulate the carry flag in arithmetic operations.

We continue by including more C type definitions:

```cpp
typedef unsigned char Byte;
typedef unsigned short Word;
typedef unsigned long long Qword;
```
Now we define a struct called `Instruction`. This will serve as a container which will holds the 1 byte CPU operation code and an additional 2 bytes values, which can represent either
immediate data or a pointer to memory.
```cpp
typedef struct {
	Byte inst;
	Word ptr;
}Instruction;
```
And now we define our MOS6502 CPU struct it contains the main components of the CPU: `Accumulator`, `Index Register X`, `Index Register Y`, `Stack Pointer`, `Program Counter`, `Processor 
Status`, `Memory` and `CPU Cycles Counter`. It is worth noting that in real hardware design, memory (RAM) is not part of the CPU itself. we could model memory as a separate structure. 
However, for the sake of simplicity, we include it directly inside the the CPU and store it as an array on the host
```
typedef struct {
	Byte A, X, Y;
	Byte SP;
	Word PC;
	Byte flags;
	Byte mem[0xFFFF];
	Qword cycles;
}M6502;
```

```cpp
typedef struct {
    Byte A, X, Y;     // Accumulator, Index Register X and Y
    Byte SP;          // Stack Pointer
    Word PC;          // Program Counter
    Byte flags;       // Process Status
    Byte mem[0xFFFF]; // 6502 Memory on the host stack
    Qword cycles;     // CPU cycles useful to count amount of instruction done by the CPU
}M6502;
```

Emulating a CPU or even simple one could be easily acheivable for simple CPU instructions 
