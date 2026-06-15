import sys

def xxd(infile, outfile):
    with open(infile, 'rb') as f, open(outfile, 'w', encoding='utf-8') as out:
        addr = 0
        while True:
            chunk = f.read(16)
            if not chunk:
                break
            
            hex_parts = [f"{b:02x}" for b in chunk]
            hex_str = " ".join(hex_parts)
            if len(chunk) < 16:
                # pad hex representation to align ASCII output
                # xxd aligns the ASCII text at column 51 (8 chars address, 2 chars ': ', 16*3 - 1 chars hex, 2 chars '  ')
                # Wait, address: 8 chars
                # ': ': 2 chars
                # hex: 16 * 2 chars = 32. 15 spaces = 15. Total hex block is 47 chars.
                # So if len(chunk) is L:
                # hex_str has L * 2 + (L - 1) chars = 3 * L - 1.
                # Remaining spaces to reach 47: 48 - 3 * L (since 47 - (3*L - 1) = 48 - 3*L).
                # Let's verify: if L = 16, 48 - 48 = 0 spaces.
                # If L = 15, 48 - 45 = 3 spaces.
                # Let's test with L = 15: hex_str has 44 chars. With 3 spaces, it has 47 chars. Perfect!
                padding = " " * (48 - 3 * len(chunk))
                hex_str += padding
            
            ascii_parts = []
            for b in chunk:
                if 32 <= b <= 126:
                    ascii_parts.append(chr(b))
                else:
                    ascii_parts.append('.')
            ascii_str = "".join(ascii_parts)
            
            out.write(f"{addr:08x}: {hex_str}  {ascii_str}\n")
            addr += 16

if __name__ == '__main__':
    infile = sys.argv[1] if len(sys.argv) > 1 else 'campus.db'
    outfile = sys.argv[2] if len(sys.argv) > 2 else 'campus.hex'
    xxd(infile, outfile)
