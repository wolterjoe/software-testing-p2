#include <string.h>
#include <stdlib.h>
#include <stdio.h>

void main(int argc, char** argv)
{
	int ret = system("rm syncfile");
	char* program;
	if (argc != 2)
	{
		printf("Usage: chess_runner program \n");
		return;
	}
	program = argv[1];
	char buf[1024] = "";
	//printf("%s", buffer);
	strcat(buf, "./run.sh ");
	strncat(buf, program, strlen(program));
	//strncat(buf, " > /dev/null", strlen(" > /dev/null"));
	//printf("%s", buf);
	fprintf(stderr, "Recording Synchronization Points...\n");
	ret = system(buf);

	int synclen = 0;
	FILE* read = fopen("syncs", "r");
	if(read != NULL)
	{
		printf("\n");
		fscanf(read, "%d", &synclen);
		fclose(read);
		int i=0;
		for(i=0; i<synclen; i++)
		{
			FILE* write = fopen("syncfile", "w");
			fprintf(write, "%d\n", i);
			fclose(write);
			fprintf(stderr, "Running Test %d of %d...\n", i, synclen - 1);
			int ret = system(buf);
		}
	}else
	{
		printf("Error reading temporary file. \n");
	}
	
}