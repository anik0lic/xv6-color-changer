// Console input and output.
// Input is from the keyboard or serial port.
// Output is written to the screen and serial port.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "traps.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"

static void consputc(int);

static int panicked = 0;

static struct
{
	struct spinlock lock;
	int locking;
} cons;

static void
printint(int xx, int base, int sign)
{
	static char digits[] = "0123456789abcdef";
	char buf[16];
	int i;
	uint x;

	if (sign && (sign = xx < 0))
		x = -xx;
	else
		x = xx;

	i = 0;
	do
	{
		buf[i++] = digits[x % base];
	} while ((x /= base) != 0);

	if (sign)
		buf[i++] = '-';

	while (--i >= 0)
		consputc(buf[i]);
}

// Print to the console. only understands %d, %x, %p, %s.
void cprintf(char *fmt, ...)
{
	int i, c, locking;
	uint *argp;
	char *s;

	locking = cons.locking;
	if (locking)
		acquire(&cons.lock);

	if (fmt == 0)
		panic("null fmt");

	argp = (uint *)(void *)(&fmt + 1);
	for (i = 0; (c = fmt[i] & 0xff) != 0; i++)
	{
		if (c != '%')
		{
			consputc(c);
			continue;
		}
		c = fmt[++i] & 0xff;
		if (c == 0)
			break;
		switch (c)
		{
		case 'd':
			printint(*argp++, 10, 1);
			break;
		case 'x':
		case 'p':
			printint(*argp++, 16, 0);
			break;
		case 's':
			if ((s = (char *)*argp++) == 0)
				s = "(null)";
			for (; *s; s++)
				consputc(*s);
			break;
		case '%':
			consputc('%');
			break;
		default:
			// Print unknown % sequence to draw attention.
			consputc('%');
			consputc(c);
			break;
		}
	}

	if (locking)
		release(&cons.lock);
}

void panic(char *s)
{
	int i;
	uint pcs[10];

	cli();
	cons.locking = 0;
	// use lapiccpunum so that we can call panic from mycpu()
	cprintf("lapicid %d: panic: ", lapicid());
	cprintf(s);
	cprintf("\n");
	getcallerpcs(&s, pcs);
	for (i = 0; i < 10; i++)
		cprintf(" %p", pcs[i]);
	panicked = 1; // freeze other CPU
	for (;;)
		;
}

#define A(x) ((x) - '@') // Alt + x
#define BACKSPACE 0x100
#define CRTPORT 0x3d4
static ushort *crt = (ushort *)P2V(0xb8000); // CGA memory

static void
cgaputc(int c)
{
	int pos;
	int static color = 0x0700;
	int static flag = 1;
	int static selected = 7;
	int static newPos;
	int row = 80;

	ushort menu[9][9];
	ushort static back[9][9];

	ushort fst[7] = {'W', 'H', 'T', ' ', 'B', 'L', 'K'};
	ushort sec[7] = {'P', 'U', 'R', ' ', 'W', 'H', 'T'};
	ushort trd[7] = {'R', 'E', 'D', ' ', 'A', 'Q', 'U'};
	ushort fth[7] = {'W', 'H', 'T', ' ', 'Y', 'E', 'L'};

	ushort *m[4] = {fst, sec, trd, fth};
	int iM = 3;
	int jM = 0;

	for (int i = 0; i < 9; i++)
	{
		for (int j = 0; j < 9; j++)
		{
			if (i % 2 == 0)
			{
				menu[i][j] = '-';
			}
			else
			{
				if (j == 0 || j == 8)
					menu[i][j] = ':';
				else
					menu[i][j] = m[iM][jM++];
			}
		}
		if (i % 2 != 0)
		{
			iM--;
			jM = 0;
		}
	}

	// Cursor position: col + 80*row.
	outb(CRTPORT, 14);
	pos = inb(CRTPORT + 1) << 8;
	outb(CRTPORT, 15);
	pos |= inb(CRTPORT + 1);

	if (c == '\n' && flag)
			pos += 80 - pos % 80;
	else if (c == BACKSPACE)
	{
		if (pos > 0 && flag)
			--pos;
	}
	else if (c == A('C'))
	{
		if (flag)
		{
			if (pos % 80 >= 70)
				newPos = pos - 10;
			else
				newPos = pos;

			for (int i = 8; i >= 0; i--)
			{
				for (int j = 0; j < 9; j++)
				{
					back[i][j] = crt[newPos - (row - j - 1)];
				}
				row += 80;
			}

			flag--;
		}
		else
		{
			for (int i = 8; i >= 0; i--)
			{
				for (int j = 0; j < 9; j++)
				{
					c = back[i][j];
					crt[newPos - (row - j - 1)] = c;
				}
				row += 80;
			}

			switch (selected)
			{
			case 7:
				color = 0x0700;
				break;
			case 5:
				color = 0x7500;
				break;
			case 3:
				color = 0x3400;
				break;
			case 1:
				color = 0x6700;
				break;
			}

			flag++;
		}
	}
	else
	{
		if (flag)
			crt[pos++] = (c & 0xff) | color;
	}

	if (flag == 0)
	{
		row = 80;

		if (c == 'W' || c == 'w')
		{
			selected = (selected + 2) % 8;
		}
		else if (c == 'S' || c == 's')
		{
			selected -= 2;
			if (selected < 0)
				selected += 8;
		}

		for (int i = 0; i < 9; i++)
		{
			for (int j = 0; j < 9; j++)
			{
				c = menu[i][j];
				if (i == selected)
					crt[newPos - (row - j - 1)] = (c & 0xff) | 0x2000;
				else
					crt[newPos - (row - j - 1)] = (c & 0xff) | 0x7000;
			}
			row += 80;
		}
	}

	if (pos < 0 || pos > 25 * 80)
		panic("pos under/overflow");

	if ((pos / 80) >= 24)
	{ // Scroll up.
		memmove(crt, crt + 80, sizeof(crt[0]) * 23 * 80);
		pos -= 80;
		memset(crt + pos, 0, sizeof(crt[0]) * (24 * 80 - pos));
	}

	outb(CRTPORT, 14);
	outb(CRTPORT + 1, pos >> 8);
	outb(CRTPORT, 15);
	outb(CRTPORT + 1, pos);
	crt[pos] = ' ' | 0x0700;
}

void consputc(int c)
{
	if (panicked)
	{
		cli();
		for (;;)
			;
	}

	if (c == BACKSPACE)
	{
		uartputc('\b');
		uartputc(' ');
		uartputc('\b');
	}
	else
		uartputc(c);
	cgaputc(c);
}

#define INPUT_BUF 128
struct
{
	char buf[INPUT_BUF];
	uint r; // Read index
	uint w; // Write index
	uint e; // Edit index
} input;

#define C(x) ((x) - '@') // Control-x

void consoleintr(int (*getc)(void))
{
	int c, doprocdump = 0;

	acquire(&cons.lock);
	while ((c = getc()) >= 0)
	{
		switch (c)
		{
		case C('P'): // Process listing.
			// procdump() locks cons.lock indirectly; invoke later
			doprocdump = 1;
			break;
		case C('U'): // Kill line.
			while (input.e != input.w &&
				   input.buf[(input.e - 1) % INPUT_BUF] != '\n')
			{
				input.e--;
				consputc(BACKSPACE);
			}
			break;
		case C('H'):
		case '\x7f': // Backspace
			if (input.e != input.w)
			{
				input.e--;
				consputc(BACKSPACE);
			}
			break;
		case A('C'):
			consputc(A('C'));
			break;
		default:
			if (c != 0 && input.e - input.r < INPUT_BUF)
			{
				c = (c == '\r') ? '\n' : c;
				input.buf[input.e++ % INPUT_BUF] = c;
				consputc(c);
				if (c == '\n' || c == C('D') || input.e == input.r + INPUT_BUF)
				{
					input.w = input.e;
					wakeup(&input.r);
				}
			}
			break;
		}
	}
	release(&cons.lock);
	if (doprocdump)
	{
		procdump(); // now call procdump() wo. cons.lock held
	}
}

int consoleread(struct inode *ip, char *dst, int n)
{
	uint target;
	int c;

	iunlock(ip);
	target = n;
	acquire(&cons.lock);
	while (n > 0)
	{
		while (input.r == input.w)
		{
			if (myproc()->killed)
			{
				release(&cons.lock);
				ilock(ip);
				return -1;
			}
			sleep(&input.r, &cons.lock);
		}
		c = input.buf[input.r++ % INPUT_BUF];
		if (c == C('D'))
		{ // EOF
			if (n < target)
			{
				// Save ^D for next time, to make sure
				// caller gets a 0-byte result.
				input.r--;
			}
			break;
		}
		*dst++ = c;
		--n;
		if (c == '\n')
			break;
	}
	release(&cons.lock);
	ilock(ip);

	return target - n;
}

int consolewrite(struct inode *ip, char *buf, int n)
{
	int i;

	iunlock(ip);
	acquire(&cons.lock);
	for (i = 0; i < n; i++)
		consputc(buf[i] & 0xff);
	release(&cons.lock);
	ilock(ip);

	return n;
}

void consoleinit(void)
{
	initlock(&cons.lock, "console");

	devsw[CONSOLE].write = consolewrite;
	devsw[CONSOLE].read = consoleread;
	cons.locking = 1;

	ioapicenable(IRQ_KBD, 0);
}
