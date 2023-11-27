#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

int do_debug = 1;
int do_info = 1;
int do_error = 1;

#define DEBUG(M, ...) if(do_debug){fprintf(stdout, "[DEBUG] " M, ##__VA_ARGS__);}
#define INFO(M, ...) if(do_info){fprintf(stdout, M, ##__VA_ARGS__);}
#define ERROR(M, ...) if(do_error){fprintf(stderr, "[ERROR] (%s:%d) " M, __FILE__, __LINE__, ##__VA_ARGS__);}


#define MAX_STATE_POOL  1024
#define MAX_OUT_LIST_POOL  1024
#define MAX_GROUP_STACK 256
#define MAX_STATE_OUT   1024

#define MAX_REGEX 256

#define CONCAT_SYM '&'



enum StateType {
    STATE_TYPE_MATCH,   // no output
    STATE_TYPE_CHAR,    // one output to next state
    STATE_TYPE_SPLIT,   // two outputs to next states
};

enum
{
	STATE_MATCH = 256,
	STATE_SPLIT = 257
};

struct State {
    int c;
    struct State *out;
    struct State *out1;
    int lastlist;

    // check if state is allocated
    char is_alloc;
};

struct OutList {
    struct State **s;
    struct OutList *next;
};

struct Group {
    struct State *start;
    struct OutList *out;


    //struct State **out[MAX_STATE_OUT];
    char is_alloc;

};

struct NFA {
    struct State spool[MAX_STATE_POOL];
};

struct State* nfa_state_init(struct NFA *nfa, int c, struct State *s_out, struct State *s_out1)
{
    /* Find unused state in pool */
    struct State *s = nfa->spool;
    for (int i=0 ; i<MAX_STATE_POOL ; i++, s++) {
        if (!s->is_alloc) {
            s->is_alloc = 1;
            s->c = c;
            s->out = s_out;
            s->out1 = s_out1;
            return s;
        }
    }
    ERROR("Max states reached: %d\n", MAX_STATE_POOL);
    return NULL;
}

void state_debug(struct State *s, int level)
{
    const int spaces = 2;

    if (s == NULL) {
        //printf("-> NULL\n");
        return;
    }
    for (int i=0 ; i<level*spaces ; i++)
        printf(" ");

    switch (s->c) {
        case STATE_MATCH:
            printf("  MATCH!\n");
            return;
        case STATE_SPLIT:
            printf("  SPLIT\n");
            break;
        default:
            printf("  State: '%c'\n", s->c);
            break;

    }
    state_debug(s->out, level+1);
    state_debug(s->out1, level+1);
}

void stack_debug(struct Group *stack, size_t size)
{
    DEBUG("** STACK ***********************\n");
    for (int i=0 ; i<size ; i++) {
        if (stack[i].start == NULL)
            break;
        DEBUG("%d => %c\n", i, stack[i].start->c);
    }
}

struct NFA nfa_init() {
    struct NFA nfa;
    memset(&nfa, 0, sizeof(struct NFA));
    return nfa;
}

struct OutList* ol_init(struct OutList *l, struct State **s)
{
    l->s = s;
    l->next = NULL;
    return l;
}

struct Group group_init(struct NFA *nfa, struct State *s_start, struct OutList *out)
{
    /*                     GROUP
     *            -------------------------
     *            |                       |
     *            |    ---------------    |    OUT LINKED LIST
     *    START   |    |         out |----|---->X
     *      X<----|    |    STATE    |    |
     *            |    |        out1 |->X |
     *            |    ---------------    |
     *            |                       |
     *            -------------------------
     */
    struct Group g;
    memset(&g, 0, sizeof(struct Group));
    g.start = s_start;

    // pointer to first item in linked list
    g.out = out;
    return g;
}

void group_patch_outlist(struct Group *g, struct State **s)
{
    struct OutList *lp = g->out;
    while (lp != NULL) {
        *(lp->s) = *s;
        lp = lp->next;
    }
}

struct OutList* outlist_join(struct OutList *l0, struct OutList *l1)
{
    /* Joint out of  to start of g1 */
    struct OutList *bak = l0;
    while (l0->next != NULL)
        l0 = l0->next;

    l0->next = l1;
    return bak;
}

struct State* nfa_compile(struct NFA *nfa, const char *pattern)
{
    /* Create NFA from pattern */

    // Groups are chained states
    // The group can be treated as a black box with one start point
    // and many out points.
    // This is how we can split states.
    // The out points need to be connected to a start point
    // of the next group
    // This will create a tree structure
    //
    // Groups are pushed onto the stack. We wait for a meta char
    // and then decide how the group should be treated
    // When done, the group is pushed back to the stack

    // A normal char pushes a group onto the stack
    // A meta char pops one or two groups from the stack
    struct Group stack[MAX_GROUP_STACK];
    struct OutList pool[MAX_OUT_LIST_POOL];
    struct Group *stackp = stack;
    struct Group g, g0, g1; // the paths groups take from stack

    struct State *s;
    struct OutList *outlistp = pool;
    struct OutList *l;

    #define PUSH(S) *stackp++ = S
    #define POP()   *--stackp
    #define GET_OL() outlistp++

    DEBUG("Compiling pattern: '%s'\n", pattern);

    for (const char *p=pattern ; *p ; p++) {
        switch (*p) {
            case '&':       // concat
                g1 = POP();
                g0 = POP();
                group_patch_outlist(&g0, &g1.start);
                g = group_init(nfa, g0.start, g1.out);
                PUSH(g);
                break;
            case '?':       // zero or one
                g = POP();
                s = nfa_state_init(nfa, STATE_SPLIT, g.start, NULL);
                l = ol_init(GET_OL(), &s->out1);
                l = outlist_join(g.out, l);
                g = group_init(nfa, s, l);
                PUSH(g);
                break;
            case '|':       // alternate
                g1 = POP();
                g0 = POP();
                s = nfa_state_init(nfa, STATE_SPLIT, g0.start, g1.start);
                l = outlist_join(g0.out, g1.out);
                PUSH(group_init(nfa, s, l));

                break;
            case '*':       // zero or more
                g = POP();
                s = nfa_state_init(nfa, STATE_SPLIT, g.start, NULL);
                group_patch_outlist(&g, &s);
                l = ol_init(GET_OL(), &s->out1);
                PUSH(group_init(nfa, g.start, l));
                break;
            case '+':       // one or more
                g = POP();
                s = nfa_state_init(nfa, STATE_SPLIT, g.start, NULL);
                group_patch_outlist(&g, &s);
                l = ol_init(GET_OL(), &s->out1);
                PUSH(group_init(nfa, s, l));
                break;
            default:        // it is a normal character
                s = nfa_state_init(nfa, *p, NULL, NULL);
                l = ol_init(GET_OL(), &s->out);
                g = group_init(nfa, s, l);
                PUSH(g);
                break;
        }
    }

    stack_debug(stack, MAX_GROUP_STACK);

    g = POP();

    // connect last state that indicates a succesfull match
    struct State *match_state = nfa_state_init(nfa, STATE_MATCH, NULL, NULL);

    group_patch_outlist(&g, &match_state);

    state_debug(g.start, 0);

    #undef POP
    #undef PUSH
    #undef GET_OL

    return g.start;
}

/* Enum is ordered in order of precedence, do not change order!!!
 * Higher number is higher precedence so we can easily compare.
 * Precedence HIGH->LOW: (|&?*+
 * */
enum REMetaChar {
    RE_META_UNDEFINED = 256,

    // QUANTIFIERS (in order of precedence) don't change this!!!!!
    RE_META_PLUS,       //  +   GREEDY     match preceding 1 or more times
    RE_META_STAR,       //  *   GREEDY     match preceding 0 or more times
    RE_META_QUESTION,   //  ?   NON GREEDY match preceding 1 time            when combined with another quantifier it makes it non greedy
    RE_CONCAT,          // explicit concat symbol
    RE_META_PIPE,       //  |   OR
    //////////////////////////
                         //
    RE_META_RANGE_START,  // {n}  NON GREEDY match preceding n times
    RE_META_RANGE_END,    // {n}  NON GREEDY match preceding n times
    RE_META_GROUP_START,  // (
    RE_META_GROUP_END,    // )
    RE_META_CCLASS_START, // [
    RE_META_CCLASS_END,   // ]
                         
    // OPERATORS
    RE_META_CARET,        // ^  can be NEGATE|BEGIN
    RE_META_NEGATE,       // ^
                          //
    RE_META_BACKSLASH,    // \ backreference, not going to implement
    RE_META_BEGIN,        // ^
    RE_META_END,          // $
    RE_META_DOT,          // .    any char except ' '
                          
    RE_CHAR,             // literal char

                         
    RE_MATCH_DIGIT,            // \d   [0-9]
    RE_MATCH_NON_DIGIT,        // \D   [^0-9]
    RE_MATCH_ALPHA_NUM,        // \w   [a-bA-B0-9]
    RE_MATCH_NON_ALPHA_NUM,    // \W   [^a-bA-B0-9]
    RE_MATCH_SPACE,            // \s   ' ', \n, \t, \r
    RE_MATCH_NON_SPACE,        // \S   ^' '

                             //
};

int re_get_meta_char(const char **s)
{
    /* Reads first meta char from  string.
     * If one char meta or char, increment pointer +1
     * If two char meta, increment pointer +2 */
    //assert(strlen(*s) > 0);

    char c = **s;

    if (strlen(*s) > 1 && **s == '\\') {
        c = *((*s)+1);
        (*s)+=2;
        switch (c) {
            case 'd':
                return RE_MATCH_DIGIT;
            case 'D':
                return RE_MATCH_NON_DIGIT;
            case 'w':
                return RE_MATCH_ALPHA_NUM;
            case 'W':
                return RE_MATCH_ALPHA_NUM;
            case 's':
                return RE_MATCH_SPACE;
            case 'S':
                return RE_MATCH_NON_SPACE;
            default:
                return RE_CHAR;
        }
    }

    (*s)++;
    switch (c) {
        case '*':
            return RE_META_STAR;
        case '+':
            return RE_META_PLUS;
        case '?':
            return RE_META_QUESTION;
        case '{':
            return RE_META_RANGE_START;
        case '}':
            return RE_META_RANGE_END;
        case '(':
            return RE_META_GROUP_START;
        case ')':
            return RE_META_GROUP_END;
        case '[':
            return RE_META_CCLASS_START;
        case ']':
            return RE_META_CCLASS_END;
        case '|':
            return RE_META_PIPE;
        case '\\':
            return RE_META_BACKSLASH;
        // decide between BEGIN and NEGATE
        case '^':
            return RE_META_CARET;
        case '$':
            return RE_META_END;
        case '.':
            return RE_META_DOT;
        case CONCAT_SYM:
            return RE_CONCAT;
        default:
            return c;
    }
}

void debug_reg(int *arr, size_t len, int *pos)
{
    int *p = arr;
    for (int i=0 ; i<len ; i++, p++) {
        if (!*p)
            break;

        if (p == pos)
            break;

        if (i != 0)
            printf(" ");

        switch (*p) {
            case RE_CONCAT:
                printf("%c", CONCAT_SYM);
                break;
            case RE_META_GROUP_START:
                printf("(");
                break;
            case RE_META_STAR:
                printf("*");
                break;
            case RE_META_PLUS:
                printf("+");
                break;
            case RE_META_QUESTION:
                printf("?");
                break;
            case RE_META_PIPE:
                printf("|");
                break;
            case RE_MATCH_ALPHA_NUM:
                printf("\\w");
                break;
            case RE_MATCH_DIGIT:
                printf("\\d");
                break;
            case RE_MATCH_SPACE:
                printf("\\s");
                break;
            default:
                printf("%c", *p);
                break;
        }
    }
    printf("\n");
}

char* re2post(char *re)
{
	int nalt, natom;
	static char buf[8000];
	char *outputp;
	struct {
		int nalt;
		int natom;
	} paren[100], *p;
	
	p = paren;
	outputp = buf;
	nalt = 0;
	natom = 0;
	if(strlen(re) >= sizeof buf/2)
		return NULL;
	for(; *re; re++){
		switch(*re){
		case '(':
			if(natom > 1){
				--natom;
				*outputp++ = '.';
			}
			if(p >= paren+100)
				return NULL;
			p->nalt = nalt;
			p->natom = natom;
			p++;
			nalt = 0;
			natom = 0;
			break;
		case '|':
			if(natom == 0)
				return NULL;
			while(--natom > 0)
				*outputp++ = '.';
			nalt++;
			break;
		case ')':
			if(p == paren)
				return NULL;
			if(natom == 0)
				return NULL;
			while(--natom > 0)
				*outputp++ = '.';
			for(; nalt > 0; nalt--)
				*outputp++ = '|';
			--p;
			nalt = p->nalt;
			natom = p->natom;
			natom++;
			break;
		case '*':
		case '+':
		case '?':
			if(natom == 0)
				return NULL;
			*outputp++ = *re;
			break;
		default:
			if(natom > 1){
				--natom;
				*outputp++ = '.';
			}
			*outputp++ = *re;
			natom++;
			break;
		}
	}
	if(p != paren)
		return NULL;
	while(--natom > 0)
		*outputp++ = '.';
	for(; nalt > 0; nalt--)
		*outputp++ = '|';
	*outputp = 0;
	return buf;
}

int infix_to_postfix(const char *expr)
{
    // intermediate operator stack
    // output queue
    // input array
    //
    // OP precedence: (|&?*+^
    //
    // Read TOKEN
    //   if LETTER => add to queue
    //   if OP
    //     while OP on top of stack with grater precedence
    //       pop OP from stack into output queue
    //     push OP onto stack
    //   if LBRACKET => push onto stack
    //   if RBRACKET
    //     while not LBRACKET on top of stack
    //       pop OP from stack into output queue
    //      pop LBRACKET from stack and throw away
    //
    // when done
    //   while stack not empty
    //     pop OP from stack to queue
    //
    // read: https://gist.github.com/gmenard/6161825
    // read: https://gist.github.com/DmitrySoshnikov/1239804/ba3f22f72d7ea00c3a662b900ded98d344d46752
    // yt:   https://www.youtube.com/watch?v=QzVVjboyb0s

    DEBUG("Parsing: %s\n", expr);


    // start by adding explicit CAT symbols
    int regex_expl[MAX_REGEX]; // regex chars with added concat symbols
    memset(regex_expl, 0, sizeof(int) * MAX_REGEX);

    int *p_out = regex_expl;
    const char **p_in = &expr;

    while (strlen(*p_in)) {
        DEBUG("len: %ld\n", strlen(*p_in));

        int c0 = re_get_meta_char(p_in);
        int c1 = re_get_meta_char(p_in);

        *p_out++ = c0;
        if (!c1)
            break;

        DEBUG("chars: '%c' '%c'\n", c0, c1);

        if (c0 != RE_META_GROUP_START &&
            c1 != RE_META_GROUP_END &&
            c1 != RE_META_PIPE &&
            c1 != RE_META_QUESTION &&
            c1 != RE_META_PLUS &&
            c1 != RE_META_STAR &&
            c1 != RE_META_CARET &&
            c0 != RE_META_CARET &&
            c0 != RE_META_PIPE)
            *p_out++ = RE_CONCAT;

        (*p_in)--;
    }

    DEBUG("Regex in explicit CAT notation: ");
    debug_reg(regex_expl, MAX_REGEX, p_out);


    // Turn it into reverse Polish notation (RPN)
    int outq[MAX_REGEX];       // ouput queue
    int opstack[MAX_REGEX];    // intermediate storage stack for operators

    memset(outq, 0, sizeof(int) * MAX_REGEX);
    memset(opstack, 0, sizeof(int) * MAX_REGEX);

    int *outqp = outq;
    int *stackp = opstack;

    int op;

    // track how many pipes we've seen and if they're in () or not
    int op_pipe = 0;
    int in_group = 0;

    #define PUSH(S) *stackp++ = S
    #define POP()   *--stackp
    #define PUSH_OUT(S) *outqp++ = S

    int *c = regex_expl;

    while (*c) {

        DEBUG("CHAR: '%c'\n", *c);
        // TODO put concat symbols in

        switch (*c) {
            case RE_META_GROUP_START:
                DEBUG("GROUP START\n");
                PUSH(*c);
                in_group = 1;
                break;
            case RE_META_GROUP_END:
                DEBUG("GROUP END\n");
                while ((op = POP()) != RE_META_GROUP_START)
                    PUSH_OUT(op);
                for (int i=0 ; i<op_pipe ; i++) {
                    DEBUG("PUT PIPE\n");
                    PUSH_OUT(RE_META_PIPE);
                }
                op_pipe = 0;
                in_group = 0;
                break;
            case RE_META_PIPE:
                op_pipe++;
                break;
            case RE_CONCAT:
                DEBUG("CONCAT\n");
                PUSH(*c);
                break;
            case RE_META_STAR:
            case RE_META_PLUS:
            case RE_META_QUESTION:
                // PIPE is already postfix so we should put it behind next char
                while (stackp != opstack) {
                    op = POP();
                    DEBUG("POP: %c, %d\n", op, op);

                    // check precedence (operators are ordered in enum)
                    if (op >= *c) {
                        PUSH(op);
                        break;
                    }

                    PUSH_OUT(op);
                }
                PUSH_OUT(*c);
                break;
            default:
                PUSH_OUT(*c);
                break;

        }
        DEBUG("QUEUE: ");
        debug_reg(outq, MAX_REGEX, outqp);
        DEBUG("STACK: ");
        debug_reg(opstack, MAX_REGEX, stackp);
        printf("\n");
        c++;
    }

    // empty stack into out queue
    while (stackp != opstack)
        PUSH_OUT(POP());

    // put pipes
    for (int i=0 ; i<op_pipe ; i++)
        PUSH_OUT(RE_META_PIPE);

    DEBUG("QUEUE: ");
    debug_reg(outq, MAX_REGEX, outqp);
    DEBUG("STACK: ");
    debug_reg(opstack, MAX_REGEX, stackp);
    printf("\n");

    return 0;

    #undef PUSH
    #undef POP
    #undef PUSH_OUT
}

int main()
{
    //infix_to_postfix("a&b&c");
    //infix_to_postfix("ab(b|c|d)?a+b");
    //infix_to_postfix("(b|c|d)a+|bbc");
    DEBUG("out: %s\n", re2post("abcdef"));
    infix_to_postfix("abcdef");


    return 1;

    struct NFA nfa = nfa_init();
    nfa_compile(&nfa, "ab&b&b&b&bb&e&e&|");
    return 1;




}
