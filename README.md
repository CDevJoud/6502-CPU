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
Status`, `Memory` and `CPU Cycles Counter`. **It is worth noting that in real hardware design, memory (RAM) is not part of the CPU itself. we could model memory as a separate structure. 
However, for the sake of simplicity, we include it directly inside the the CPU and store it as an array on the host**
```cpp
typedef struct {
	Byte A, X, Y; 		// Acummulator, Register X and Y
	Byte SP;	  		// Stack Pointer
	Word PC;	  		// Program Counter
	Byte flags;	  		// Processor Status
	Byte mem[0xFFFF];   // MOS 6502 memory on the host stack
	Qword cycles;		// CPU cycles counter
}M6502;
```

## Function Definitions
The first function that we need is a way to clear memory or an array, the C standard library does provide `void* __cdecl memset(void* _Dst, int _Val, size_t _Size)` to clear memory but
because this project is simple enough we don't really need to depend on any sort of libraries at all so we can implement our function instead:
```cpp
/*******************************************************************************
	This function iterates over a block of memory and sets each byte to zero.
	It will be used to initialize or reset memory when needed
*******************************************************************************/
void zOut(
	Byte* mem,
	const Qword size
) {
	for (Qword i = 0; i < size; i++) {
		*(mem + i) = 0;
	}
}
```

Now we need 3 more functions they will server a way to read and write to the MOS 6502 memory, they are `readByte()`, `readWord()` and `writeByte`
```cpp
Byte readByte(
    M6502* cpu,
    Word addr
) {
    // Check if the address is within valid memory range
    // If valid, return the byte at that address
    // Otherwise, return 0x00 as a safe default
    return (addr >= 0x0000 && addr <= 0xFFFF) ? *(cpu->mem + addr) : 0x00;
}

Word readWord(
    M6502* cpu,
    Word addr
) {
    // Read the low byte from the given address
    Byte low = readByte(cpu, addr);

    // Read the high byte from the next address
    Byte high = readByte(cpu, (Word)(addr + 1));

    // Combine low and high bytes into a 16-bit value (little-endian)
    return (Word)((Word)low | ((Word)high << 8));
}

void writeByte(
    M6502* cpu,
    Word addr,
    Byte value
) {
    // Only write if the address is within valid memory range
    if (addr >= 0x0000 && addr <= 0xFFFF) {
        // Store the value at the given memory location
        *(cpu->mem + addr) = value;
    }
}
```

And the last 2 functions are `fetchInstruction()` and `executeInstruction()`. These will we leave them empty as we will handle all our CPU logic in there
```cpp

Instruction fetchInstruction(
	M6502* cpu
) {
	// Empty
}

int executeInstruction(
	M6502* cpu,
	Instruction inst
) {
	// Empty
}

```
Seperating these 2 functions is **important** because it follows how a real CPU operates internally. A CPU doesn't execute instructions directly from memory in one step, intead it first
fetches the instruction and the executes it.

The `fetchInstruction()` function is responsible for reading the next instruction from memory using the program counter and preparing it for execution, on the other hand 
`executeInstruction()` takes that instruction and performs the correct operation such as modifying registers, updating flags, or changing the program counter.

By keeping these 2 steps separate, the design becomes clearer and easier to understand. It also makes the system more flexible, since it change how instructions are read or executed.

## Implementing Basic Instructions
Before implementing any CPU instructions we need a function that reset our CPU state!

```cpp
void init6502(
	M6502* cpu
) {
	cpu->A = cpu->X = cpu->Y = 0;
	cpu->PC = 0x0000;
	cpu->flags = FLAG_Z;
	cpu->SP = 0x01FF;
	cpu->cycles = 0;
	zOut(cpu->mem, 0xFFFF);
}
```
This function initializes the CPU to a known starting state.

It resets all registers(A, X, Y) to zero, sets the Program Counter `PC` to the address `0x0000`, and initializes the flags register. The stack pointer is set to
it's default position, and the entire memory is cleared to zero.

---
### LDX_IMM

The first instruction we will cover is the load X Register with immediate addressing mode, in code:
```cpp
#define LDX_IMM 0xA2
```
What this instruction does, it load the X register with immediate data instead of loading from a memory address, for example `LDX #$05` the value 5 get inserted in the X register
and the immediate mode is marked by using `#`. If we wanted to load from a memory address we would use `LDX $0005`. You might also see that `LDX $05` and `LDX $0005` are the same thing but 
they are actually 2 different instructions and it depends on how the assembler interpret them.

Now we update `fetchInstruction()` and `executeInstruction` with `LDX_IMM`:
```cpp
Instruction fetchInstruction(
	M6502* cpu
) {
	// We prepare an instruction object
	Instruction inst;
	zOut(&inst, sizeof(Instruction));

	// We read a single byte to determine what kind of instruction and check if it's a special address mode
	inst.inst = readByte(cpu, cpu->PC++);
	switch(inst.inst) {
	default:
		inst.ptr = readByte(cpu, cpu->PC++);
		break;
	};
	return inst;
}
```

```cpp
int executeInstruction(
	M6502* cpu,
	Instruction inst
) {
	switch(inst.inst) {
	// LDX with immediate mode addressing example LDX #$05
	case LDX_IMM:
    {
		// we insert the immediate value to the register directly
        cpu->X = inst.ptr;
		// Set the appropriate flags
        SET_FLAG(cpu, FLAG_Z, IS_ZERO(cpu->X));
        SET_FLAG(cpu, FLAG_N, IS_NEGATIVE(cpu->X));
		// How many cpu cycles it took to perform the action
		cpu->cycles += 2;
    }break;
	};
	return !GET_FLAG(cpu, FLAG_B);
}
```

Now we will create a MOS 6502 cpu instance in the main entry point.
```cpp
int main() {
	M6502 cpu;
	init6502(&cpu);

	// we write our first 6502 assembly program that load X register with the value 5
	cpu.mem[0x0000] = LDX_IMM;
	cpu.mem[0x0001] = 0x05;

	int loop = 1;
	while (loop > 0) {
		Instruction inst = fetchInstruction(&cpu);
		loop = executeInstruction(&cpu, inst);
	}

	// we return the value of X register which is 5
	return cpu.X;	
}
```

The above code was the simplest 6502 assembly program but you will notice that it loops infinitely. The reason is that we previously cleared the whole memory with zero and there is no logic 
in the `executeInstruction` to deal with that value so it just skips it and loops infinitely. The fix is to introduce a break instruction.

## BRK
We define The break instructions as follow
```cpp
#define BRK 0x00
```

Now we update `executeInstruction` with `BRK`:

```cpp
int executeInstruction(
	M6502* cpu,
	Instruction inst
) {
	switch(inst.inst) {
	// ...
	// above code
	case BRK:
    {
        cpu->cycles += 7; // see for more detail https://6502.org/users/obelisk/6502/reference.html#BRK
		SET_FLAG(cpu, FLAG_B, 1);
    }break;
	// ...
}
```
Now when we launch our program u will notice it exits with a return value of 5!
Cool! Now how do we get it to printing `Hello World!`?

## High level algorithm of writing `Hello World!`
First we start by setting an index register (X) to 0. This index will be used to go through each character in the string.

Then, we load a character from memory using the base address of the string plus the index. This gives us the current character we wamt to process.

Next, we check if the character is equal to 0. This value is used as a terminator to mark the end of the string.

If the value is 0 the program stops. Otherwise, the character is written to the output location in memory.

After theat, we increment the index register so that we can move to the next character.

This process repeats until the end of the string is reached.

### The 6502 assembly code would look like this
```asm
		LDX #$00		; X = 0 (string index)
LOOP:	LDA $000F,X		; load character from string base + X
		BEQ END			; if character == 0, stop

		STA $0200,X		; write character to output memory
		INX 			; X++

		JMP LOOP		; repeat

END:	BRK				; stop execution

		.byte "Hello World!", 0
```
There are 2 important instructions that need to be covered and the first one is:
```asm
	LDA $000F,X
```
This instruction loads a value from memory into the accumulator using a 16-bit base address, adds the X register to it, and reads the byte from that final address into the accumulator
```asm
	STA $0200,X
```
The second instruction stores the value of the accumulator into memory using absolute addressing with X indexing.
So the CPU calculates the final address using the base address + X, then writes the accumulator value into that memory location.

---
but the problem is we don't have an assembler to assemble our code! luckily I have already assembled it by hand but before I could provide it, first we need to define the new instructions 
that we have discovered!
```cpp
#define LDA_ABS_X 0xBD
#define BEQ 0xF0
#define STA_ABS_X 0x9D
#define INX 0xE8
#define JMP 0x4C
```
And the assembled code:
```cpp
const Byte app[29] = {
    LDX_IMM, 0x00,
    LDA_ABS_X, 0x0F, 0x00,
    BEQ, 0x07, // Performs a relative jump if the Zero flag is set, otherwise execution continues normally.
    STA_ABS_X, 0x00, 0x02,
    INX, // Increment the X register by one
    JMP, 0x02, 0x00, // This instructions jump to a memory location, basically it changes the Program Counter to new memory address
    BRK, // End of execution

    'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o', 'r', 'l', 'd', '!', 0
};
```

Now we need to update our `fetchInstruction()` and `executeInstruction()`:
```cpp
Instruction fetchInstruction(
	M6502* cpu
) {
	// code
	// ...


	inst.inst = readByte(cpu, cpu->PC++);
	switch(inst.inst) {

	case LDA_ABS_X:
	{
		// Read 16-bit base address (low + high byte) from memory
		inst.ptr = readWord(cpu, cpu->PC);

		// Move PC forward by 2 bytes (size of address)
		cpu->PC += 2;
	} break;

	case STA_ABS_X:
	{
		// Read 16-bit base address where value will be stored
		inst.ptr = readWord(cpu, cpu->PC);

		// Advance PC past the address
		cpu->PC += 2;
	} break;

	case JMP:
	{
		// Read 16-bit target address to jump to
		inst.ptr = readWord(cpu, cpu->PC);

		// Advance PC past the address (execution will later jump)
		cpu->PC += 2;
	} break;

	case INX:
	{
		// INX has no operand, so no extra bytes to read
		inst.ptr = 0;
	} break;
	default:
		inst.ptr = readByte(cpu, cpu->PC++);
		break;
	};
	return inst;
}
```

```cpp
int executeInstruction(
	M6502* cpu,
	Instruction inst
) {
	switch (inst.inst) {
		// ...
		// above code
	case LDA_ABS_X:
	{
		// Calculate final address (base + X offset)
		Word addr = inst.ptr + cpu->X;

		// Load value from memory into accumulator
		cpu->A = readByte(cpu, addr);

		// Update Zero and Negative flags based on result
		SET_FLAG(cpu, FLAG_Z, IS_ZERO(cpu->A));
		SET_FLAG(cpu, FLAG_N, IS_NEGATIVE(cpu->A));

		// Base cycle cost
		cpu->cycles += 4;

		// Add extra cycle if page boundary is crossed
		if (PAGE_CROSSED(inst.ptr, addr)) {
			cpu->cycles++;
		}
	} break;

	case BEQ:
	{
		// Relative offset for branch
		Byte offset = inst.ptr;

		// Base cycle cost
		cpu->cycles += 2;

		// Branch only if Zero flag is set
		if (GET_FLAG(cpu, FLAG_Z)) {
			Word base = cpu->PC;

			// Calculate new address using relative offset
			Word addr = base + (Byte)offset;

			cpu->cycles++;

			// Extra cycle if page boundary is crossed
			if (PAGE_CROSSED(base, addr)) {
				cpu->cycles++;
			}

			// Update program counter (perform branch)
			cpu->PC = addr;
		}
	} break;

	case STA_ABS_X:
	{
		// Calculate final address (base + X offset)
		inst.ptr += cpu->X;

		// Store accumulator value into memory
		writeByte(cpu, inst.ptr, cpu->A);

		// Base cycle cost
		cpu->cycles += 5;
	} break;

	case INX:
	{
		// Increment X register
		cpu->X++;

		// Update Zero and Negative flags
		SET_FLAG(cpu, FLAG_Z, IS_ZERO(cpu->X));
		SET_FLAG(cpu, FLAG_N, IS_NEGATIVE(cpu->X));

		// Cycle cost
		cpu->cycles += 2;
	} break;

	case JMP:
	{
		// Jump to target address
		cpu->PC = inst.ptr;

		// Cycle cost
		cpu->cycles += 3;
	} break;
}

```
In this step, we implemented a small set of instructions that are enough to create a simple program.

`LDA_ABS_X` loads a value from memory into the accumulator using an address with an X offset.

`STA_ABS_X` stores the value of the accumulator back into memory, also using an X offset. In our case, this is used to write characters to the output memory.

`INX` increments the X register, which allows us to move through memory step by step.

`BEQ` performs a relative jump if the Zero flag is set, which is used to detect the end of the string.

`JMP` performs an unconditional jump, allowing us to create a loop.

Together these instructions allow us to iterate through a string in memory, output each character, and stop when we reach the end.

---

Now we need a function that loads our assembled program to M6502 memory
```cpp
void loadToM6502Memory(
	M6502 *cpu,
	const Word addr,
	const void *bin,
	Qword size
) {
    Byte* d = cpu->mem + addr;
    const Byte* s = bin;

    while(size--) {
        *d++ = *s++;
    }
}
```
This function loads a block of data into the CPU memory starting at a given address. It copies bytes from a source buffer into the CPU memory one by one, and is used to place programs or 
data (such as our “Hello World” application) into memory before execution begins.

Now on our main entry point we will use our `loadToM6502Memory` to copy our 6502 program to it's memory
```cpp
const Byte app[29] = {
	LDX_IMM, 0x00,
	LDA_ABS_X, 0x0F, 0x00,
	BEQ, 0x07,
	STA_ABS_X, 0x00, 0x02,
	INX,
	JMP, 0x02, 0x00,
	BRK,

	'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o', 'r', 'l', 'd', '!', 0
};

int main() {
	M6502 cpu;
	init6502(&cpu);

	// For simplicity the destination address would be at 0x0000
	loadToM6502Memory(&cpu, 0x0000, app, 29);

	// start the execution
	int loop = 1;
	while (loop > 0) {
		Instruction inst = fetchInstruction(&cpu);
		loop = executeInstruction(&cpu, inst);
	}

	return cpu.X;
}
```

Congratulations! Now we are done!
---
If we compile and run the code, we will notice that the instructions are executed, but there is no visible output.

This happens because we are writing the result to memory, not directly to the standard output. To see the result, we need to read from the memory location where the output was written and 
print it.

After the while loop we can add a `printf()` call:
```cpp

int main() {
	// code ...

	while(loop > 0) {
		// code ...
	}
	printf("%s", cpu.mem + 0x0200); // 0x0200 is where we output our string on the M6502 memory
	return cpu.X; // optional
}

```

The output result will be:
```
Hello World!
```

## Reflection
This project shows how a simple CPU model can be used to understand basic concepts of low-level programming. By implementing a small set of instructions, it becomes easier to see how 
programs are executed step by step using registers, memory, and flags.

One of the more difficult parts is understanding control flow, especially how instructions like `BEQ` change the program counter using relative jumps. The use of flags can also be harder to 
follow at first, since they affect program behavior indirectly.

Overall, this approach helps explain how a CPU works in a simple and clear way. It shows that program execution is based on small operations, and that more complex behavior is built on top 
of these basic ideas.

Final [source code](m6502.c)
