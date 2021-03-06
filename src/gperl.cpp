#include <gperl.hpp>
using namespace std;

GPerlMemoryManager *mm;
GPerlUndef *undef;

GPerl::GPerl(int argc, char **argv)
{
	init();
	if (argc < 2) {
		startInteractiveMode();
	} else {
		startEvalScriptMode(argc, argv);
	}
}

GPerl::~GPerl()
{
	free_cwb(cwb);
}

void GPerl::init(void)
{
	//cwb = (char *)safe_malloc(MAX_CWB_SIZE);
	cwb = init_cwb(MAX_CWB_SIZE);
	mm = new GPerlMemoryManager();
	undef = new_GPerlUndef();
}

int GPerl::checkBrace(char *line)
{
	int i = 0;
	while (line[i] != EOL) {
		if (line[i] == '{' || line[i] == '(') {
			brace_count++;
		} else if (line[i] == '}' || line[i] == ')') {
			brace_count--;
		}
		i++;
	}
	return brace_count;
}

void GPerl::startInteractiveMode(void)
{
	usingHistory();
	brace_count = 0;
	char *line = NULL;
	char tmp[128] = {0};
	while (true) {
		if (line == NULL) {
			line = greadline(">>> ");
		}
		if (!strncmp(line, "quit", sizeof("quit")) ||
			!strncmp(line, "exit", sizeof("exit"))) {
			exit(0);
		}
		addHistory(line);
		int check = checkBrace(line);
		if (check == 0) {
			strcat(tmp, " ");
			strcat(tmp, line);
			if (line[strlen(line)-1] != '}') {
				strcat(tmp, ";");
			}
			GPerlValue ret = eval(tmp);
			switch (TYPE_CHECK(ret)) {
			case 0: /* Double */
				fprintf(stdout, "%f\n", ret.dvalue);
				break;
			case 1: /* Int */
				fprintf(stdout, "%d\n", ret.ivalue);
				break;
			case 2: /* String */
				fprintf(stdout, "%s\n", getRawString(ret));
				break;
			default: /* Other Object */
				break;
			}
			tmp[0] = EOL;
			free(line);
			line = NULL;
		} else if (check > 0) {
			strcat(tmp, " ");
			strcat(tmp, line);
			free(line);
			line = greadline("...");
		} else {
			fprintf(stderr, "Syntax error!!\n");
			tmp[0] = EOL;
			free(line);
			line = NULL;
			brace_count = 0;//brace_count is global variable
		}
	}
}

void GPerl::startEvalScriptMode(int argc, char **argv)
{
	const char *filename = argv[1];
	char line[MAX_LINE_SIZE] = {0};
	char script_[MAX_SCRIPT_SIZE] = {0};
	char *tmp = script_;
	char *script = script_;
	FILE *fp = fopen(filename, "r");
	if (!fp) {
		fprintf(stderr, "script not found: %s\n", filename);
		exit(EXIT_FAILURE);
	}
	while (fgets(line, MAX_LINE_SIZE, fp) != NULL) {
		//DBG_PL("line = [%s]", line);
		int line_size = strlen(line);
		snprintf(tmp, line_size + 1, "%s\n", line);
		tmp += line_size;
	}
	eval(script, argc, argv);
	fclose(fp);
}

GPerlValue GPerl::eval(char *script, int argc, char **argv)
{
	GPerlTokenizer t;
	std::vector<GPerlToken *> *tokens = t.tokenize(script);
	t.annotateTokens(tokens);
    t.prepare(tokens);
    GPerlToken *root = t.parseSyntax(NULL, tokens);
    DBG_PL("=============<dump syntax>============");
    t.dumpSyntax(root, 0);
    t.insertParenthesis(tokens);
	DBG_PL("=============<TOKENIZE>============");
	t.dump(tokens);
	GPerlParser *p;
	if (argv) {
		p = new GPerlParser(tokens, argc - 2, argv + 2);
	} else {
		p = new GPerlParser(tokens);
	}
	DBG_PL("==============<PARSE>==============");
	GPerlAST *ast = p->parse();
	if (p->pkgs->size() > 0) {
		for (size_t i = 0; i < p->pkgs->size(); i++) {
			ast->add(p->pkgs->at(i));
		}
	}
#ifdef USING_GRAPH_DEBUG
	//ast->show();//graph debug with graphviz
#endif
	GPerlCompiler compiler;
	DBG_PL("=============<COMPILE>=============");
	GPerlVirtualMachineCode *codes = compiler.compile(ast, NULL);
	DBG_PL("-----------<DUMP VMCODE>-----------");
	compiler.dumpPureVMCode(codes);
	DBG_PL("-----------------------------------");
	GPerlVirtualMachine *vm = new GPerlVirtualMachine(compiler.clses);
	DBG_PL("=============<RUNTIME>=============");
	mm->switchGC();
	vm->run(codes, NULL);//create threading code
	return vm->run(codes, compiler.jit_params);//execute code
}
