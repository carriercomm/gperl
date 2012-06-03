typedef enum {
	Return,
	Add,
	Sub,
	Mul,
	Div,
	Greater,
	Less,
	GreaterEqual,
	LessEqual,
	EqualEqual,
	NotEqual,
	VarDecl,
	FunctionDecl,
	Assign,
	BuiltinFunc,
	IfStmt,
	ElseStmt,
	Comma,
	SemiColon,
	LeftParenthesis,
	RightParenthesis,
	LeftBrace,
	RightBrace,
	LeftBracket,
	RightBracket,
	Shift,
	CallDecl,
	WhileStmt,
	FieldDecl,
	TypeRef,
	LabelRef,
	LocalVarDecl,
	GlobalVarDecl,
	Var,
	ArrayVar,
	Int,
	Float,
	String,
	Object,
	Array,
	Operator,
	LocalVar,
	LocalArrayVar,
	GlobalVar,
	GlobalArrayVar,
	Function,
	Call,
	Argument,
	List,
	Undefined,
} GPerlT;

typedef struct _GPerlTokenInfo {
	GPerlT type;
	const char *name;
	const char *data;
} GPerlTokenInfo;
