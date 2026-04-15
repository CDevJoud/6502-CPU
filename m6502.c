#define FLAG_C 0b00000001 // Carry
#define FLAG_Z 0b00000010 // Zero
#define FLAG_I 0b00000100 // Interrupt Disable
#define FLAG_D 0b00001000 // Decimal 
#define FLAG_B 0b00010000 // Break
#define FLAG_U 0b00100000 // Useless
#define FLAG_V 0b01000000 // Overflow
#define FLAG_N 0b10000000 // Negative

#define SET_FLAG(cpu, flag, cond) (cpu->flags = (cond) ? (cpu->flags | flag) : (cpu->flags & ~flag))
#define GET_FLAG(cpu, flag) (cpu->flags & flag)
#define PAGE_CROSSED(base, addr) ((base & 0xFF00) != (addr & 0xFF00))
#define IS_ZERO(r) (r == 0)
#define IS_NEGATIVE(r) (r & 0b10000000)
#define IS_CARRY(r) (r >= 0b10000000)

#define LDX_IMM 0xA2
#define BRK 0x00
#define LDA_ABS_X 0xBD
#define BEQ 0xF0
#define STA_ABS_X 0x9D
#define INX 0xE8
#define JMP 0x4C

typedef unsigned char Byte;
typedef unsigned short Word;
typedef unsigned long long Qword;

typedef struct {
	Byte inst;
	Word ptr;
}Instruction;

typedef struct {
	Byte A, X, Y; 		// Acummulator, Register X and Y
	Byte SP;	  		// Stack Pointer
	Word PC;	  		// Program Counter
	Byte flags;	  		// Processor Status
	Byte mem[0xFFFF];   // MOS 6502 memory on the host stack
	Qword cycles;		// CPU cycles counter
}M6502;

void zOut(
	Byte* mem,
	const Qword size
) {
	for (Qword i = 0; i < size; i++) {
		*(mem + i) = 0;
	}
}

/*******************************************************************************
	This function iterates over a block of memory and sets each byte to zero.
	It will be used to initialize or reset memory when needed
*******************************************************************************/

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


Instruction fetchInstruction(
	M6502* cpu
) {
	// We prepare an instruction object
	Instruction inst;
	zOut(&inst, sizeof(Instruction));

	// We read a single byte to determine what kind of instruction and check if it's a special address mode
	inst.inst = readByte(cpu, cpu->PC++);
	switch (inst.inst) {

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
	return inst;
}

int executeInstruction(
	M6502* cpu,
	Instruction inst
) {
	switch (inst.inst) {
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

	case BRK:
	{
		cpu->cycles += 7; // see for more detail https://6502.org/users/obelisk/6502/reference.html#BRK
		SET_FLAG(cpu, FLAG_B, 1);
	}
	};
	return !GET_FLAG(cpu, FLAG_B);
}

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

void loadToM6502Memory(
	M6502* cpu,
	const Word addr,
	const void* bin,
	Qword size
) {
	Byte* d = cpu->mem + addr;
	const Byte* s = bin;

	while (size--) {
		*d++ = *s++;
	}
}

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

#include <stdio.h>

int main() {
	M6502 cpu;
	init6502(&cpu);

	// For simplicity the destination address would be at 0x0000
	loadToM6502Memory(&cpu, 0x0000, app, 29);
	
	int loop = 1;
	while (loop > 0) {
		Instruction inst = fetchInstruction(&cpu);
		loop = executeInstruction(&cpu, inst);
	}

	printf("%s", cpu.mem + 0x0200); // 0x0200 is where we output our string on the M6502 memory
	return cpu.X; // optional
}
