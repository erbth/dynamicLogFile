#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/select.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#define FIFO_FILE "debug"
#define LINE_BUFFER_SIZE 40

struct line
{
	char *szLine;
	size_t sLength;
};

struct lineBuffer
{
	int nEntries;
	int currLine;

	// 'Array' of pointers to line entries.
	struct line **list;
};

// Prototypes
void debug_lineBuffer (struct lineBuffer *pBuf);

// Reads a line from a filestream
// TODO: This function may block if no newline char is written.
struct line *readLine (FILE *file)
{
	struct line *ln;
	size_t cap = 0;
	char *szLine = NULL;
	ssize_t length;

	// Read line
	length = getline (&szLine, &cap, file);

	if (length < 0)
	{
		// No error, but nothing to read.
		if (errno == EAGAIN || errno == EWOULDBLOCK)
		{
			// Clear error on stream
			clearerr (file);
			return NULL;
		}

		perror ("getline");
		return NULL;
	}

	// Same here
	if (length == 0)
	{
		return NULL;
	}

	// Allocate line info structure
	if ( (ln = calloc (1, sizeof (struct line))) == NULL)
	{
		perror ("calloc");
		return NULL;
	}

	ln->szLine = szLine;
	ln->sLength = length;
	return ln;
}

// Frees a line
void freeLine (struct line **ppL)
{
	if (ppL && *ppL)
	{
		free ((*ppL)->szLine);
		free (*ppL);
		*ppL = NULL;
	}
}

// Creates a lineBuffer
struct lineBuffer *create_lineBuffer (int nEntries)
{
	struct lineBuffer *pBuf;

	if (! (pBuf = calloc (1, sizeof (struct lineBuffer))))
	{
		perror ("calloc");
		return NULL;
	}

	pBuf->nEntries = nEntries;
	pBuf->currLine = 0;	// actually redundant, but for completeness

	pBuf->list = calloc (nEntries, sizeof (struct line));
	if (!pBuf->list)
	{
		perror ("calloc");
		free (pBuf);
		return NULL;
	}

#ifdef DEBUG
	debug_lineBuffer (pBuf);
#endif

	return pBuf;
}

// Destroy a lineBuffer
void destroy_linebuffer (struct lineBuffer **ppBuf)
{
	if (ppBuf && *ppBuf)
	{
		struct lineBuffer *pBuf = *ppBuf;
		int i;

		for (i = 0; i < pBuf->nEntries; i++)
		{
			if (pBuf->list[i] != NULL)
			{
				freeLine (&(pBuf->list[i]));
			}
		}

		free (pBuf);
		*ppBuf = NULL;
	}
}

// Debug lineBuffer
void debug_lineBuffer (struct lineBuffer *pBuf)
{
	int i;

	fprintf (stderr, "lineBuffer:\n");

	for (i = 0; i < pBuf->nEntries; i++)
	{
		fprintf (stderr,
			"  %p%s\n",
			pBuf->list[i],
			(pBuf->currLine == i) ? " <--" : "");
	}

	fprintf (stderr, "\n");
}

// Stores a line in the line buffer.
void storeLine (struct lineBuffer *pBuf, struct line **ppLn)
{
	if (ppLn && *ppLn)
	{
		struct line *pLn = *ppLn;

		// If current line is already in use, overwrite.
		if (pBuf->list[pBuf->currLine] != NULL)
		{
			freeLine (&(pBuf->list[pBuf->currLine]));
		}

		pBuf->list[pBuf->currLine] = pLn;
		pBuf->currLine = (pBuf->currLine + 1) % pBuf->nEntries;
		*ppLn = NULL;

#ifdef DEBUG
		debug_lineBuffer (pBuf);
#endif
	}
}

// Prints stored lines
void printLines (struct lineBuffer *pBuf, FILE* file)
{
	int i;

	// Loop through list and print line values.
	i = pBuf->currLine;

	do
	{
		if (pBuf->list[i] != NULL && pBuf->list[i]->szLine != NULL)
		{
			fprintf (file, "%s", pBuf->list[i]->szLine);
		}

		i = (i + 1) % pBuf->nEntries;
	}
	while (i != pBuf->currLine);
}

int main (int argc, char *argv[])
{
	int returnStatus = EXIT_FAILURE;
	struct stat statBuf;
	FILE *fFifo = NULL;
	int fdFifo;
	fd_set readfds;
	int maxFd;
	struct lineBuffer *pLnBuf = NULL;

	fprintf (stderr, "dynamic logfile v1.0\n");
	fprintf (stderr, "Abort with q <ENTER> and print last 10 lines with p <ENTER>.\n");

	// Create line buffer
	pLnBuf = create_lineBuffer (LINE_BUFFER_SIZE);
	if (!pLnBuf)
	{
		fprintf (stderr, "couldn't create line buffer.\n");
		goto END;
	}

	// Create fifo if it doesn't exist already.
	memset (&statBuf, 0, sizeof (statBuf));

	if (stat (FIFO_FILE, &statBuf) != 0)
	{
		if (errno == ENOENT)
		{
			// File not existing, create
			if (mkfifo (FIFO_FILE, S_IWUSR | S_IRUSR) != 0)
			{
				perror ("mkfifo");
				goto END;
			}
		}
		else
		{
			perror ("stat");
			goto END;
		}
	}
	else
	{
		if (! (S_ISFIFO (statBuf.st_mode)))
		{
			fprintf (stderr, "Error: file %s exists but is not a fifo. Exiting.\n", FIFO_FILE);
			goto END;
		}
	}

	// Print fifo name
	printf ("%s\n", FIFO_FILE);

	// Open fifo for reading.
	// see http://stackoverflow.com/questions/14594508/fifo-pipe-is-always-readable-in-select
	fdFifo = open (FIFO_FILE, O_RDWR | O_NONBLOCK);
	if (fdFifo < 0)
	{
		perror ("open");
		goto END;
	}

	fFifo = fdopen (fdFifo, "r");
	if (!fFifo)
	{
		perror ("fdopen");
		close (fdFifo);	// otherwise the fd will be close by fclose().
		goto END;
	}

	// Wait for data and save last n lines.
	if (STDIN_FILENO > fdFifo)
	{
		maxFd = STDIN_FILENO;
	}
	else
	{
		maxFd = fdFifo;
	}

	maxFd++;

	for (;;)
	{
		FD_ZERO (&readfds);
		FD_SET (STDIN_FILENO, &readfds);
		FD_SET (fdFifo, &readfds);

		if (select (maxFd, &readfds, NULL, NULL, NULL) < 1)
		{
			fprintf (stderr, "select terminated with exit status < 1\n");
			perror ("select");
			break;
		}

		if (FD_ISSET (STDIN_FILENO, &readfds))
		{
			char c = getchar ();

			if (c == 'q')
			{
				fprintf (stderr, "bye.\n");
				break;
			}
			else if (c == 'p')
			{
				printLines (pLnBuf, stderr);
			}
		}

		if (FD_ISSET (fdFifo, &readfds))
		{
			// Read ALL pending data
			struct line *ln;

			while (ln = readLine (fFifo))
			{
				storeLine (pLnBuf, &ln);
			}
		}
	}

	// No fatal errors yet, return sucessfully
	returnStatus = EXIT_SUCCESS;

END:
	if (pLnBuf)
	{
		destroy_linebuffer (&pLnBuf);
	}

	if (fFifo)
	{
		fclose (fFifo);
	}

	fprintf (stderr, "exiting ...\n");

	return returnStatus;
}
