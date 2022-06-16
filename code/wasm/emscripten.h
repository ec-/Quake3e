union cvalue
{
	int i;
	intptr_t p;
  char *c;
};

int CreateAndCall(const char *code, const char *params, ...);

#define EM_ASM_INT(code, args...) CreateAndCall(#code, #args, args);
#define EM_ASM_ARGS(code, args...) CreateAndCall(#code, #args, args);
#define EM_ASM_(code, args...) CreateAndCall(#code, #args, args);
#define EM_ASM_INT_V(code) CreateAndCall(#code, "");
