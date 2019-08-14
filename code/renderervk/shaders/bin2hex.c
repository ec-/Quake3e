#include <stdio.h>
#include <stdlib.h>

int main(int argc, const char* argv[]) {
    const int line_length = 16;
    int size = 0;
	FILE* f;
	int c;

    if (argc != 3) {
        printf("Usage: bin2hex <input_file> <output_array_name>\n");
        return - 1;
    }

    f = fopen(argv[1], "rb");
    if (!f) {
        printf("Could not open file for reading: %s\n", argv[1]);
        return -1;
    }

    printf("const unsigned char %s[] = {\n\t", argv[2]);

	c = fgetc(f);
    while (c != EOF) {
        printf("0x%.2X", c);

        c = fgetc(f);
        if (c != EOF) printf(", ");

        size++;
        if (size % line_length == 0) printf("\n\t");
    }

    printf("\n};\n");
    printf("const int %s_size = %i;", argv[2], size);

    if (!feof(f)) {
        printf("Could not read entire file: %s", argv[1]);
    }

    fclose(f);

    return 0;
}

