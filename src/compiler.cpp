#include <gperl.hpp>
#include <code.hpp>
using namespace std;

GPerlCompiler::GPerlCompiler(void) : dst(0), src(0), code_num(0),
									 variable_index(0), func_index(0),
									 args_count(0)
{
	declared_vname = NULL;
	for (int i = 0; i < MAX_REG_SIZE; i++) {
		reg_type[i] = Undefined;
	}
	for (int i = 0; i < MAX_VARIABLE_NUM; i++) {
		variable_names[i] = NULL;
		variable_types[i] = Undefined;
	}
	codes = new vector<GPerlVirtualMachineCode *>();
	func_code = NULL;
}

GPerlVirtualMachineCode *GPerlCompiler::compile(GPerlAST *ast)
{
	GPerlCell *root = ast->root;
	GPerlVirtualMachineCode *thcode = createTHCODE();
	addVMCode(thcode);
	for (; root; root = root->next) {
		GPerlCell *path = root;
		compile_(path);
		DBG_PL("============================");
		dst = 0;//reset dst number
	}
	GPerlVirtualMachineCode *ret = createRET();
	addVMCode(ret);
	GPerlVirtualMachineCode *undef = createUNDEF();//for threaded code
	addVMCode(undef);
	return getPureCodes(codes);
}

#define GOTO_LEFT_LEAFNODE(path) for (; path->left; path = path->left) {}
#define isRIGHT_LEAF_NODE(branch) branch->left == NULL && branch->right == NULL && branch->argsize == 0 && branch->type != IfStmt
#define isNotFALSE_STMT(branch) branch->type != IfStmt

void GPerlCompiler::compile_(GPerlCell *path)
{
	GOTO_LEFT_LEAFNODE(path);
	genVMCode(path);
	while (path->parent) {
		GPerlCell *parent = path->parent;
		GPerlCell *branch = parent->right;
		if (branch) {
			if (branch == path) return;
			if (isRIGHT_LEAF_NODE(branch)) {
				genVMCode(branch);
			} else if (isNotFALSE_STMT(parent)) {
				compile_(branch);
			}
		}
		genVMCode(parent);
		path = parent;
	}
}

void GPerlCompiler::genVMCode(GPerlCell *path) {
	GPerlVirtualMachineCode *code;
	if (path->type == Call) {
		genFunctionCallCode(path);
	} else if (path->type == IfStmt) {
		genIfStmtCode(path);
	} else if (path->type == Function) {
		genFunctionCode(path);
		return;
	}
	code = createVMCode(path);
	addVMCode(code);
	dumpVMCode(code);
}

void GPerlCompiler::genFunctionCallCode(GPerlCell *p)
{
	size_t argsize = p->argsize;
	for (size_t i = 0; i < argsize; i++) {
		compile_(p->vargs[i]);
		if (p->type == PrintDecl) {
			addWriteCode();
		} else if (p->type == Call) {
			addPushCode(i);
		}
	}
}


#define ESCAPE_CURRENT_CODE(codes, tmp) {		\
		int size = codes->size();				\
		for (int i = 0; i < size; i++) {		\
			tmp.push_back(codes->at(i));		\
		}										\
	}

#define CLEAR_CURRENT_CODE(codes) {				\
		code_num = 0;							\
		codes->clear();							\
	}

#define ADD_FUNCCODE_TO_CODES(codes) {									\
		GPerlCell *body_stmt = path->body->root;						\
		for (; body_stmt; body_stmt = body_stmt->next) {				\
			GPerlCell *path = body_stmt;								\
			compile_(path);												\
			dst = 0;													\
		}																\
		GPerlVirtualMachineCode *ret = createRET();						\
		addVMCode(ret);													\
		GPerlVirtualMachineCode *undef = createUNDEF();/*for threaded code*/ \
		addVMCode(undef);												\
	}

#define COPY_CURRENT_CODE(codes, func_code) {	\
		size_t size = codes->size();			\
		for (size_t i = 0; i < size; i++) {		\
			func_code->push_back(codes->at(i));	\
		}										\
	}

#define REVERT_ESCAPED_CODE(tmp, codes) {		\
		size_t size = tmp.size();				\
		for (size_t i = 0; i < size; i++) {		\
			codes->push_back(tmp.at(i));		\
		}										\
	}

void GPerlCompiler::genFunctionCode(GPerlCell *path)
{
	GPerlVirtualMachineCode *code;
	vector<GPerlVirtualMachineCode *> tmp;
	vector<GPerlVirtualMachineCode *> *func_code = NULL;
	dst = 0;//reset dst number
	args_count = 0;
	func_code = new vector<GPerlVirtualMachineCode *>();
	code = createVMCode(path);//OPFUNCSET
	addVMCode(code);
	dumpVMCode(code);
	ESCAPE_CURRENT_CODE(codes, tmp);//codes => tmp
	CLEAR_CURRENT_CODE(codes);
	ADD_FUNCCODE_TO_CODES(codes);
	COPY_CURRENT_CODE(codes, func_code);
	optimizeFuncCode(func_code, path->fname);
	//finalCompile(func_code);
	GPerlVirtualMachineCode *f = getPureCodes(func_code);
	DBG_PL("========= DUMP FUNC CODE ==========");
	dumpPureVMCode(f);
	DBG_PL("===================================");
	CLEAR_CURRENT_CODE(codes);
	REVERT_ESCAPED_CODE(tmp, codes);//tmp => codes
	code = codes->back();//OPFUNCSET
	code->func = f;
	DBG_PL("========= FUNCTION DECL END ==========");
}

void GPerlCompiler::genIfStmtCode(GPerlCell *path)
{
	dst = 0;//reset dst number
	GPerlCell *true_stmt = path->true_stmt->root;
	//DBG_P("code_num = [%d], codes->size() = [%d]", code_num, codes->size());
	GPerlVirtualMachineCode *jmp = codes->at(code_num - 1);
	//DBG_PL("jmp = [%d]", jmp->op);
	int cond_code_num = code_num;
	//DBG_PL("-------------TRUE STMT--------------");
	for (; true_stmt; true_stmt = true_stmt->next) {
		GPerlCell *path = true_stmt;
		compile_(path);
		dst = 0;//reset dst number
	}
	jmp->jmp = code_num - cond_code_num + 2/*OPNOP + OPJMP + 1*/;
	int cur_code_num = code_num;
	//fprintf(stderr, "cur_code_num = [%d]\n", cur_code_num);
	jmp = createJMP(1);
	addVMCode(jmp);
	dumpVMCode(jmp);
	dst = 0;//reset dst number
	DBG_PL("-------------FALSE STMT-------------");
	if (path->false_stmt) {
		GPerlCell *false_stmt = path->false_stmt->root;
		for (; false_stmt; false_stmt = false_stmt->next) {
			GPerlCell *path = false_stmt;
			compile_(path);
			dst = 0;//reset dst number
		}
	}
	jmp->jmp = code_num - cur_code_num;
	DBG_PL("------------------------------------");
}

void GPerlCompiler::addVMCode(GPerlVirtualMachineCode *code)
{
	codes->push_back(code);
}

void GPerlCompiler::popVMCode()
{
	codes->pop_back();
}

void GPerlCompiler::addWriteCode(void)
{
	GPerlVirtualMachineCode *code;
	switch (reg_type[0]) {
	case Int:
		code = createiWRITE();
		addVMCode(code);
		dumpVMCode(code);
		break;
	case String:
		code = createsWRITE();
		addVMCode(code);
		dumpVMCode(code);
		break;
	case Object:
		//type check
		if (reg_type[1] == Int) {
			code = createiWRITE();
		} else {
			code = createoWRITE();
		}
		addVMCode(code);
		dumpVMCode(code);
		break;
	default:
		break;
	}
}

void GPerlCompiler::addPushCode(int i)
{
	GPerlVirtualMachineCode *code;
	switch (reg_type[0]) {
	case Int:
		code = createiPUSH(i);
		addVMCode(code);
		dumpVMCode(code);
		break;
	case String:
		code = createsPUSH(i);
		addVMCode(code);
		dumpVMCode(code);
		break;
	case Object:
		code = createiPUSH(i);//TODO
		addVMCode(code);
		dumpVMCode(code);
		break;
	default:
		break;
	}
}

void GPerlCompiler::optimizeFuncCode(vector<GPerlVirtualMachineCode *> *f, string fname)
{
	vector<GPerlVirtualMachineCode *>::iterator it = f->begin();
	//int reg_n = 0;
	//bool isOMOVCall = false;
	while (it != f->end()) {
		GPerlVirtualMachineCode *c = *it;
		if (c->op == OPJCALL && fname == c->name) {
			c->op = OPJSELFCALL;
		} else if (c->op == OPNOP) {
			it = f->erase(it);
			code_num--;
			it--;
		} else if (c->op == OPSET) {
			it = f->erase(it);
			code_num--;
			it--;
		} else if (c->op == OPSHIFT) {
			it = f->erase(it);
			code_num--;
			it--;
		} else if (c->op == OPLET) {
			it = f->erase(it);
			code_num--;
			it--;
		} else if (c->op == OPOMOV) {
			/*
			reg_n = c->dst;
			if (isOMOVCall) {
				//TODO
				it = f->erase(it);
				code_num--;
				it--;
				isOMOVCall = false;
			} else {
				//c->op = OPSUPERCALL;
				isOMOVCall = true;
			}
			*/
		}
		it++;
	}
}

#define CONCAT(c0, c1, c2) OP##c0##c1##c2
#define CONCAT2(c0, c1) OP##c0##c1

#define DST(n) c->dst == n
#define SRC(n) c->src == n

#define OPCREATE_TYPE1(O) {						\
		if (DST(0) && SRC(1)) {					\
			c->op = CONCAT(A, B, O);			\
		} else if (DST(0) && SRC(2)) {			\
			c->op = CONCAT(A, C, O);			\
		} else if (DST(0) && SRC(3)) {			\
			c->op = CONCAT(A, D, O);			\
		} else if (DST(1) && SRC(2)) {			\
			c->op = CONCAT(B, C, O);			\
		} else if (DST(1) && SRC(3)) {			\
			c->op = CONCAT(B, D, O);			\
		} else if (DST(2) && SRC(3)) {			\
			c->op = CONCAT(C, D, O);			\
		}										\
	}

#define OPCREATE_TYPE2(O) {						\
		if (DST(0)) {							\
			c->op = CONCAT2(A, O);				\
		} else if (DST(1)) {					\
			c->op = CONCAT2(B, O);				\
		} else if (DST(2)) {					\
			c->op = CONCAT2(C, O);				\
		} else if (DST(3)) {					\
			c->op = CONCAT2(D, O);				\
		}										\
	}

#define OPCREATE_TYPE3(O) {						\
		if (SRC(0)) {							\
			c->op = CONCAT2(A, O);				\
		} else if (SRC(1)) {					\
			c->op = CONCAT2(B, O);				\
		} else if (SRC(2)) {					\
			c->op = CONCAT2(C, O);				\
		} else if (SRC(3)) {					\
			c->op = CONCAT2(D, O);				\
		}										\
	}

void GPerlCompiler::finalCompile(vector<GPerlVirtualMachineCode *> *code)
{
	DBG_PL("********* finalCompile ***********");
	vector<GPerlVirtualMachineCode *>::iterator it = code->begin();
	while (it != code->end()) {
		GPerlVirtualMachineCode *c = *it;
		switch (c->op) {
		/*========= TYPE1 =========*/
		case OPADD:
			OPCREATE_TYPE1(ADD);
			break;
			/*
		case OPiADD:
			OPCREATE_TYPE1(iADD);
			break;
		case OPSUB:
			OPCREATE_TYPE1(SUB);
			break;
		case OPiSUB:
			OPCREATE_TYPE1(iSUB);
			break;
		*/
		/*========= TYPE2 =========*/
/*
		case OPiADDC:
			OPCREATE_TYPE2(iADDC);
			break;
*/
		case OPiSUBC:
			OPCREATE_TYPE2(iSUBC);
			break;
		case OPiJLC:
			OPCREATE_TYPE2(iJLC);
			break;
/*
		case OPiJGC:
			OPCREATE_TYPE2(iJGC);
			break;
		case OPPUSH:
			OPCREATE_TYPE2(PUSH);
			break;
*/
		case OPiPUSH:
			OPCREATE_TYPE2(iPUSH);
			break;
/*
		case OPMOV:
			OPCREATE_TYPE2(MOV);
			break;
*/
		case OPiMOV:
			OPCREATE_TYPE2(iMOV);
			break;
		case OPOMOV:
			OPCREATE_TYPE2(OMOV);
			break;
		case OPSELFCALL:
			OPCREATE_TYPE2(SELFCALL);
			break;
		case OPJSELFCALL:
			OPCREATE_TYPE2(JSELFCALL);
			break;
		/*========= TYPE3 =========*/
		case OPRET:
			OPCREATE_TYPE3(RET);
			break;
		case OPJRET:
			OPCREATE_TYPE3(JRET);
			break;
		default:
			break;
		}
		it++;
	}
}

static GPerlVirtualMachineCode pure_codes_[128];
GPerlVirtualMachineCode *GPerlCompiler::getPureCodes(vector<GPerlVirtualMachineCode *> *codes)
{
	int code_n = codes->size();
	//GPerlVirtualMachineCode pure_codes_[code_n + 1];//for O2 option
	memset(pure_codes_ + code_n + 1, 0, sizeof(GPerlVirtualMachineCode));
	for (int i = 0; i < code_n; i++) {
		pure_codes_[i] = *codes->at(i);
	}
	volatile int code_size = code_n * sizeof(GPerlVirtualMachineCode);
	GPerlVirtualMachineCode *pure_codes = (GPerlVirtualMachineCode *)malloc(code_size);
	memcpy(pure_codes, pure_codes_, code_size);
	return pure_codes;
}

#define INT(O) OPi ## O
#define INTC(O) OPi ## O ## C
#define STRING(O) OPs ## O

#define SET_OPCODE(T) {							\
		dst--;									\
		code->src = dst;						\
		switch (reg_type[dst - 1]) {			\
		case Int:								\
			code->op = INT(T);					\
			break;								\
		case Object:							\
			switch (reg_type[dst]) {			\
			case Int:							\
				code->op = INTC(T);				\
				code->src = codes->back()->src;	\
				popVMCode();					\
				code_num--;						\
				code->code_num = code_num;		\
				break;							\
			default:							\
				code->op = OP ## T;				\
				break;							\
			}									\
			break;								\
		default:								\
			code->op = OP ## T;					\
			break;								\
		}										\
		code->dst = dst - 1;					\
		code->jmp = 1;							\
	}

GPerlVirtualMachineCode *GPerlCompiler::createVMCode(GPerlCell *c)
{
	GPerlVirtualMachineCode *code = new GPerlVirtualMachineCode();
	code->code_num = code_num;
	switch (c->type) {
	case Int:
		code->dst = dst;
		code->src = c->data.idata;
		code->op = INT(MOV);
		reg_type[dst] = Int;
		dst++;
		break;
	case String:
		code->dst = dst;
		code->src = -1;
		code->name = c->data.sdata;
		code->op = STRING(MOV);
		reg_type[dst] = String;
		dst++;
		break;
	case Add:
		SET_OPCODE(ADD);
		break;
	case Sub:
		SET_OPCODE(SUB);
		break;
	case Mul:
		SET_OPCODE(MUL);
		break;
	case Div:
		SET_OPCODE(DIV);
		break;
	case Greater:
		SET_OPCODE(JG);
		break;
	case Less:
		SET_OPCODE(JL);
		break;
	case GreaterEqual:
		SET_OPCODE(JGE);
		break;
	case LessEqual:
		SET_OPCODE(JLE);
		break;
	case EqualEqual:
		SET_OPCODE(JE);
		break;
	case NotEqual:
		SET_OPCODE(JNE);
		break;
	case PrintDecl:
		code->op = OPPRINT;
		break;
	case IfStmt:
		code->op = OPNOP;
		break;
	case LocalVarDecl: case VarDecl: case GlobalVarDecl: {
		code->op = OPSET;
		code->dst = variable_index;
		code->src = 0;
		const char *name = cstr(c->vname);
		code->name = name;
		setToVariableNames(name);
		declared_vname = name;
		break;
	}
	case LocalVar: case Var: {
		const char *name = cstr(c->vname);
		int idx = getVariableIndex(name);
		switch (variable_types[idx]) {
		case Int:
			code->op = OPOiMOV;
			reg_type[dst] = Int;
			break;
		default:
			code->op = OPOMOV;
			reg_type[dst] = Object;
			break;
		}
		code->dst = dst;
		code->src = idx;
		code->name = name;
		dst++;
		break;
	}
	case Call: {
		const char *name = cstr(c->fname);
		int idx = getFuncIndex(name);
		code->op = OPJCALL;
		//code->op = OPCALL;
		code->dst = dst-1;
		code->src = idx;
		code->name = name;
		break;
	}
	case Shift:
		code->op = OPSHIFT;
		code->dst = 0;
		code->src = args_count;
		args_count++;
		break;
	case Assign: {
		code->op = OPLET;
		int idx = getVariableIndex(declared_vname);
		code->dst = idx;
		code->src = 0;
		code->name = declared_vname;
		/* ======= for Type Inference ======= */
		switch (reg_type[0]) {
		case Int:
			variable_types[idx] = Int;
			break;
		case Float:
			variable_types[idx] = Float;
			break;
		case String:
			variable_types[idx] = String;
			break;
		default:
			break;
		}
		/* ================================== */
		declared_vname = NULL;
		break;
	}
	case Function: {
		code->op = OPFUNCSET;
		code->dst = func_index;
		code->src = 0;
		const char *name = cstr(c->fname);
		code->name = name;
		setToFunctionNames(name);
		declared_fname = name;
		break;
	}
	case Return:
		code->op = OPJRET;
		//code->op = OPRET;
		code->dst = 0;
		code->src = dst-1;
		break;
	default:
		break;
	}
	code_num++;
	return code;
}

void GPerlCompiler::setToVariableNames(const char *name)
{
	variable_names[variable_index] = name;
	variable_index++;
}

void GPerlCompiler::setToFunctionNames(const char *name)
{
	func_names[func_index] = name;
	func_index++;
}

int GPerlCompiler::getVariableIndex(const char *name)
{
	int ret = -1;
	size_t name_size = strlen(name);
	for (int i = 0; i < variable_index; i++) {
		size_t v_size = strlen(variable_names[i]);
		if (v_size == name_size &&
			!strncmp(variable_names[i], name, name_size)) {
			ret = i;
			break;
		}
	}
	if (ret == -1) {
		fprintf(stderr, "COMPILE ERROR: cannot find variable name[%s]\n", name);
		exit(1);
	}
	return ret;
}

int GPerlCompiler::getFuncIndex(const char *name)
{
	int ret = -1;
	size_t name_size = strlen(name);
	for (int i = 0; i < func_index; i++) {
		size_t f_size = strlen(func_names[i]);
		if (f_size == name_size &&
			!strncmp(func_names[i], name, name_size)) {
			ret = i;
			break;
		}
	}
	if (ret == -1) {
		fprintf(stderr, "COMPILE ERROR: cannot find func name[%s]\n", name);
		exit(1);
	}
	return ret;
}

GPerlVirtualMachineCode *GPerlCompiler::createTHCODE(void)
{
	GPerlVirtualMachineCode *code = new GPerlVirtualMachineCode();
	code->code_num = code_num;
	code->op = OPTHCODE;
	code_num++;
	return code;
}

GPerlVirtualMachineCode *GPerlCompiler::createRET(void)
{
	GPerlVirtualMachineCode *code = new GPerlVirtualMachineCode();
	code->code_num = code_num;
	code->op = OPJRET;
	//code->op = OPRET;
	code_num++;
	return code;
}

GPerlVirtualMachineCode *GPerlCompiler::createUNDEF(void)
{
	GPerlVirtualMachineCode *code = new GPerlVirtualMachineCode();
	code->code_num = code_num;
	code->op = OPUNDEF;
	code_num++;
	return code;
}

GPerlVirtualMachineCode *GPerlCompiler::createiWRITE(void)
{
	GPerlVirtualMachineCode *code = new GPerlVirtualMachineCode();
	code->code_num = code_num;
	code->op = OPiWRITE;
	code_num++;
	return code;
}

GPerlVirtualMachineCode *GPerlCompiler::createsWRITE(void)
{
	GPerlVirtualMachineCode *code = new GPerlVirtualMachineCode();
	code->code_num = code_num;
	code->op = OPsWRITE;
	code_num++;
	return code;
}

GPerlVirtualMachineCode *GPerlCompiler::createoWRITE(void)
{
	GPerlVirtualMachineCode *code = new GPerlVirtualMachineCode();
	code->code_num = code_num;
	code->op = OPoWRITE;
	code_num++;
	return code;
}

GPerlVirtualMachineCode *GPerlCompiler::createiPUSH(int i)
{
	GPerlVirtualMachineCode *code = new GPerlVirtualMachineCode();
	code->code_num = code_num;
	code->op = OPiPUSH;
	code->src = i;
	code->dst = dst-1;
	code_num++;
	args_count++;
	return code;
}

GPerlVirtualMachineCode *GPerlCompiler::createsPUSH(int i)
{
	GPerlVirtualMachineCode *code = new GPerlVirtualMachineCode();
	code->code_num = code_num;
	code->op = OPsPUSH;
	code->src = i;
	code->dst = dst-1;
	code_num++;
	return code;
}

GPerlVirtualMachineCode *GPerlCompiler::createJMP(int jmp_num)
{
	GPerlVirtualMachineCode *code = new GPerlVirtualMachineCode();
	code->code_num = code_num;
	code->op = OPJMP;
	code->jmp = jmp_num;
	code_num++;
	return code;
}

void GPerlCompiler::dumpVMCode(GPerlVirtualMachineCode *code)
{
	(void)code;
	DBG_PL("L[%d] : %s [dst:%d], [src:%d], [jmp:%d], [name:%s]",
		   code->code_num, OpName(code->op), code->dst, code->src,
		   code->jmp, code->name);
}

void GPerlCompiler::dumpPureVMCode(GPerlVirtualMachineCode *c)
{
	int code_n = (codes->size() > (size_t)code_num) ? code_num : codes->size();
	for (int i = 0; i < code_n; i++) {
		dumpVMCode(&c[i]);
	}
}
