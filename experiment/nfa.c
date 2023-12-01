#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

int do_debug = 1;
int do_info = 1;
int do_error = 1;

/* Read stuff:
 * OG Ken Thompson: https://dl.acm.org/doi/10.1145/363347.363387
 * Russ Cox: https://swtch.com/~rsc/regexp/regexp1.html
 *
 * Reverse Polish Notation:
 * https://gist.github.com/gmenard/6161825
 * https://gist.github.com/DmitrySoshnikov/1239804/ba3f22f72d7ea00c3a662b900ded98d344d46752
 * https://www.youtube.com/watch?v=QzVVjboyb0s
 */

#define DEBUG(M, ...) if(do_debug){fprintf(stdout, "[DEBUG] " M, ##__VA_ARGS__);}
#define INFO(M, ...) if(do_info){fprintf(stdout, M, ##__VA_ARGS__);}
#define ERROR(M, ...) if(do_error){fprintf(stderr, "[ERROR] (%s:%d) " M, __FILE__, __LINE__, ##__VA_ARGS__);}


#define MAX_STATE_POOL  1024
#define MAX_OUT_LIST_POOL  1024
#define MAX_GROUP_STACK 256
#define MAX_STATE_OUT   1024
#define MAX_CCLASS   32

#define MAX_REGEX 256

#define CONCAT_SYM '&'


/* Enum is ordered in order of precedence, do not change order!!!
 * Higher number is higher precedence so we can easily compare.
 * Precedence HIGH->LOW: (|&?*+
 * */
enum ReTokenType {
    RE_TOK_TYPE_UNDEFINED = 0,

    // QUANTIFIERS (in order of precedence) don't change this!!!!!
    RE_TOK_TYPE_PLUS,       //  +   GREEDY     match preceding 1 or more times
    RE_TOK_TYPE_STAR,       //  *   GREEDY     match preceding 0 or more times
    RE_TOK_TYPE_QUESTION,   //  ?   NON GREEDY match preceding 1 time            when combined with another quantifier it makes it non greedy
    RE_TOK_TYPE_CONCAT,          // explicit concat symbol
    RE_TOK_TYPE_PIPE,       //  |   OR
    //////////////////////////
                         //
    RE_TOK_TYPE_RANGE_START,  // {n}  NON GREEDY match preceding n times
    RE_TOK_TYPE_RANGE_END,    // {n}  NON GREEDY match preceding n times
    RE_TOK_TYPE_GROUP_START,  // (
    RE_TOK_TYPE_GROUP_END,    // )
    RE_TOK_TYPE_CCLASS_START, // [
    RE_TOK_TYPE_CCLASS_END,   // ]
                         
    // OPERATORS
    RE_TOK_TYPE_CARET,        // ^  can be NEGATE|BEGIN
    RE_TOK_TYPE_NEGATE,       // ^
    RE_TOK_TYPE_BEGIN,        // ^

    RE_TOK_TYPE_END,          // $

    RE_TOK_TYPE_BACKSLASH,    // \ backreference, not going to implement
    RE_TOK_TYPE_DOT,          // .    any char except ' '
                          
    RE_TOK_TYPE_CHAR,             // literal char
                         
    RE_TOK_TYPE_DIGIT,            // \d   [0-9]
    RE_TOK_TYPE_NON_DIGIT,        // \D   [^0-9]
    RE_TOK_TYPE_ALPHA_NUM,        // \w   [a-bA-B0-9]
    RE_TOK_TYPE_NON_ALPHA_NUM,    // \W   [^a-bA-B0-9]
    RE_TOK_TYPE_SPACE,            // \s   ' ', \n, \t, \r
    RE_TOK_TYPE_NON_SPACE,        // \S   ^' '
                                  //
    RE_TOK_TYPE_HYPHEN,           // -   (divides a range: [a-z]

    RE_TOK_TYPE_RANGE,              // not a meta char, but represents a range
};

/* Regex expression is broken up into tokens.
 * The two chars represent things like ranges.
 * In case of character, the c1 is empty.
 * In case of operator, both are empty. */
struct ReToken {
    enum ReTokenType type;
    char c0;
    char c1;
};

enum StateType {
    STATE_TYPE_NONE,   // this is a state that is a char or an operator
    STATE_TYPE_MATCH,   // no output
    STATE_TYPE_SPLIT,   // two outputs to next states
};

struct State {
    struct ReToken *t;

    enum StateType type;          // indicate split or match state

    // struct Token *token;
    struct State *out;
    struct State *out1;
    unsigned char is_alloc;       // check if state is allocated
};

/* Holds links to endpoints of state chains that are part of a Group */
struct OutList {
    struct State **s;
    struct OutList *next;
};

/* Holds State chains */
struct Group {
    struct State *start;
    struct OutList *out;
    char is_alloc;
};

struct NFA {
    struct State spool[MAX_STATE_POOL];

    // The first node in the NFA
    struct State *start;
};

struct State* nfa_state_init(struct NFA *nfa, struct ReToken *t, enum StateType type, struct State *s_out, struct State *s_out1)
{
    /* Find unused state in pool */
    struct State *s = nfa->spool;
    for (int i=0 ; i<MAX_STATE_POOL ; i++, s++) {
        if (!s->is_alloc) {
            s->is_alloc = 1;
            s->t = t;
            s->out = s_out;
            s->out1 = s_out1;
            s->type = type;
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

    switch (s->type) {
        case STATE_TYPE_MATCH:
            printf("  MATCH!\n");
            return;
        case STATE_TYPE_SPLIT:
            printf("  SPLIT: [%d] '%c'\n", s->t->type, s->t->c0);

            // don't follow start and plus because that would create endless loop
            if (s->t->type == RE_TOK_TYPE_PLUS || s->t->type == RE_TOK_TYPE_STAR) {
                for (int i=0 ; i<level*spaces ; i++)
                    printf(" ");
                printf("    NOT FOLLOWING: %c\n", s->out->t->c0);
                state_debug(s->out1, level+1);
                return;
            }
            break;
        default:
            if (s->t->type == RE_TOK_TYPE_RANGE && s->t->c1)
                printf("  State: [%c-%c]\n", s->t->c0, s->t->c1);
            else
                printf("  State: '%c'\n", s->t->c0);
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
        DEBUG("%d => %c\n", i, stack[i].start->t->c0);
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
    /* Tie all State end pointers in outlist to s.
     * Effectively connecting endpoints in group to other group */
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

struct State* nfa_compile(struct NFA *nfa, struct ReToken *tokens)
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
    struct OutList lpool[MAX_OUT_LIST_POOL];
    struct Group *stackp = stack;
    struct Group g, g0, g1; // the paths groups take from stack

    struct State *s;
    struct OutList *outlistp = lpool;
    struct OutList *l;

    #define PUSH(S) *stackp++ = S
    #define POP()   *--stackp
    #define GET_OL() outlistp++

    //DEBUG("Compiling pattern: '%s'\n", pattern);

    for (struct ReToken *t=tokens ; t->type != RE_TOK_TYPE_UNDEFINED ; t++) {

        //for (int i=0 ; i<stackp-stack ; i++) {
        //    DEBUG("GROUP: %d ******************************\n", i);
        //    state_debug(stack[i].start, 0);
        //}

        switch (t->type) {
            case RE_TOK_TYPE_CONCAT:       // concat
                g1 = POP();
                g0 = POP();
                DEBUG("TYPE: CONCAT\n");
                group_patch_outlist(&g0, &g1.start);
                g = group_init(nfa, g0.start, g1.out);
                PUSH(g);
                break;
            case RE_TOK_TYPE_QUESTION:       // zero or one
                g = POP();
                DEBUG("TYPE: QUESTION: %c\n", g.start->t->c0);
                s = nfa_state_init(nfa, t, STATE_TYPE_SPLIT, g.start, NULL);
                l = ol_init(GET_OL(), &s->out1);
                l = outlist_join(g.out, l);
                g = group_init(nfa, s, l);
                PUSH(g);
                break;
            case RE_TOK_TYPE_PIPE:       // alternate
                g1 = POP();
                g0 = POP();
                DEBUG("TYPE: PIPE:   %c | %c\n", g0.start->t->c0, g1.start->t->c0);
                s = nfa_state_init(nfa, t, STATE_TYPE_SPLIT, g0.start, g1.start);
                l = outlist_join(g0.out, g1.out);
                PUSH(group_init(nfa, s, l));

                break;
            case RE_TOK_TYPE_STAR:       // zero or more
                g = POP();
                DEBUG("TYPE: STAR:   %c\n", g.start->t->c0);
                s = nfa_state_init(nfa, t, STATE_TYPE_SPLIT, g.start, NULL);
                group_patch_outlist(&g, &s);
                l = ol_init(GET_OL(), &s->out1);
                PUSH(group_init(nfa, g.start, l));
                break;
            case RE_TOK_TYPE_PLUS:       // one or more
                g = POP();
                DEBUG("TYPE: PLUS:   %c\n", g.start->t->c0);
                s = nfa_state_init(nfa, t, STATE_TYPE_SPLIT, g.start, NULL);
                group_patch_outlist(&g, &s);
                l = ol_init(GET_OL(), &s->out1);
                PUSH(group_init(nfa, s, l));
                break;
            default:        // it is a normal character
                DEBUG("TYPE: CHAR:   %c\n", t->c0);
                s = nfa_state_init(nfa, t, STATE_TYPE_NONE, NULL, NULL);
                l = ol_init(GET_OL(), &s->out);
                g = group_init(nfa, s, l);
                PUSH(g);
                break;
        }
    }

    stack_debug(stack, MAX_GROUP_STACK);

    g = POP();

    // connect last state that indicates a succesfull match
    struct State *match_state = nfa_state_init(nfa, NULL, STATE_TYPE_MATCH, NULL, NULL);

    group_patch_outlist(&g, &match_state);

    //state_debug(g.start, 0);
    nfa->start = g.start;

    return g.start;

    #undef POP
    #undef PUSH
    #undef GET_OL
}

struct State* nfa_compile_rec(struct NFA *nfa, struct ReToken *tokens, struct State *s_prev)
{
    struct ReToken *t = &tokens[0];
    if (t->type == RE_TOK_TYPE_UNDEFINED) {
        DEBUG("END OF TOKENS\n");
        return NULL;
    }

    struct State *s_cur;
    struct State *s_next;
    //struct State *s = nfa_state_init(nfa, t, STATE_TYPE_SPLIT, NULL, NULL);

    switch (t->type) {
        case RE_TOK_TYPE_CONCAT:       // concat
            DEBUG("TYPE: CONCAT\n");
            s_cur = nfa_state_init(nfa, t, STATE_TYPE_SPLIT, NULL, NULL);
            s_next = nfa_compile_rec(nfa, tokens+1, s_cur);
            s_cur->out = s_next;
            //s_cur->out1 = s_prev;
            return s_cur;
        case RE_TOK_TYPE_QUESTION:       // zero or one
            DEBUG("TYPE: QUESTION\n");
            break;
        case RE_TOK_TYPE_PIPE:       // alternate
            DEBUG("TYPE: PIPE\n");
            break;
        case RE_TOK_TYPE_STAR:       // zero or more
            DEBUG("TYPE: STAR\n");
            break;
        case RE_TOK_TYPE_PLUS:       // one or more
            DEBUG("TYPE: PLUS\n");
            break;
        default:        // it is a normal character
            DEBUG("TYPE: CHAR\n");
            s_cur = nfa_state_init(nfa, t, STATE_TYPE_NONE, NULL, NULL);
            s_next = nfa_compile_rec(nfa, tokens+1, s_cur);
            s_cur->out = s_next;
            return s_cur;
    }
}


char* re2post(const char *re)
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
				*outputp++ = CONCAT_SYM;
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
				*outputp++ = CONCAT_SYM;
			nalt++;
			break;
		case ')':
			if(p == paren)
				return NULL;
			if(natom == 0)
				return NULL;
			while(--natom > 0)
				*outputp++ = CONCAT_SYM;
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
				*outputp++ = CONCAT_SYM;
			}
			*outputp++ = *re;
			natom++;
			break;
		}
	}
	if(p != paren)
		return NULL;
	while(--natom > 0)
		*outputp++ = CONCAT_SYM;
	for(; nalt > 0; nalt--)
		*outputp++ = '|';
	*outputp = 0;
	return buf;
}

struct ReToken re_get_meta_char(const char **s)
{
    /* Reads first meta char from  string.
     * If one char meta or char, increment pointer +1
     * If two char meta, increment pointer +2 */
    assert(strlen(*s) > 0);

    struct ReToken tok;

    char c = **s;

    if (strlen(*s) > 1 && **s == '\\') {
        c = *((*s)+1);
        (*s)+=2;
        tok.c0 = c;
        switch (c) {
            case 'd':
                tok.type = RE_TOK_TYPE_DIGIT;
                break;
            case 'D':
                tok.type = RE_TOK_TYPE_NON_DIGIT;
                break;
            case 'w':
                tok.type = RE_TOK_TYPE_ALPHA_NUM;
                break;
            case 'W':
                tok.type = RE_TOK_TYPE_ALPHA_NUM;
                break;
            case 's':
                tok.type = RE_TOK_TYPE_SPACE;
                break;
            case 'S':
                tok.type = RE_TOK_TYPE_NON_SPACE;
                break;
            default:
                tok.type = RE_TOK_TYPE_CHAR;
                break;
        }
    }

    else {
        tok.c0 = c;
        (*s)++;
        switch (c) {
            case '*':
                tok.type = RE_TOK_TYPE_STAR;
                break;
            case '+':
                tok.type = RE_TOK_TYPE_PLUS;
                break;
            case '?':
                tok.type = RE_TOK_TYPE_QUESTION;
                break;
            case '{':
                tok.type = RE_TOK_TYPE_RANGE_START;
                break;
            case '}':
                tok.type = RE_TOK_TYPE_RANGE_END;
                break;
            case '(':
                tok.type = RE_TOK_TYPE_GROUP_START;
                break;
            case ')':
                tok.type = RE_TOK_TYPE_GROUP_END;
                break;
            case '[':
                tok.type = RE_TOK_TYPE_CCLASS_START;
                break;
            case ']':
                tok.type = RE_TOK_TYPE_CCLASS_END;
                break;
            case '|':
                tok.type = RE_TOK_TYPE_PIPE;
                break;
            case '\\':
                tok.type = RE_TOK_TYPE_BACKSLASH;
                break;
            // decide between BEGIN and NEGATE
            case '^':
                tok.type = RE_TOK_TYPE_CARET;
                break;
            case '$':
                tok.type = RE_TOK_TYPE_END;
                break;
            case '-':
                tok.type = RE_TOK_TYPE_HYPHEN;
                break;
            case '.':
                tok.type = RE_TOK_TYPE_DOT;
                break;
            case CONCAT_SYM:
                tok.type = RE_TOK_TYPE_CONCAT;
                break;
            default:
                tok.type = RE_TOK_TYPE_CHAR;
                break;
        }
    }
    return tok;
}

void debug_reg(struct ReToken *tokens)
{
    /* Print out token array */
    struct ReToken *t = tokens;
    while (t->type != RE_TOK_TYPE_UNDEFINED) {

        if (t != tokens)
            printf(" ");

        switch (t->type) {
            case RE_TOK_TYPE_CONCAT:
                printf("%c", CONCAT_SYM);
                break;
            case RE_TOK_TYPE_GROUP_START:
                printf("(");
                break;
            case RE_TOK_TYPE_GROUP_END:
                printf(")");
                break;
            case RE_TOK_TYPE_RANGE:
                printf("%c-%c", t->c0, t->c1);
                break;
            case RE_TOK_TYPE_STAR:
                printf("*");
                break;
            case RE_TOK_TYPE_PLUS:
                printf("+");
                break;
            case RE_TOK_TYPE_QUESTION:
                printf("?");
                break;
            case RE_TOK_TYPE_PIPE:
                printf("|");
                break;
            case RE_TOK_TYPE_ALPHA_NUM:
                printf("\\w");
                break;
            case RE_TOK_TYPE_DIGIT:
                printf("\\d");
                break;
            case RE_TOK_TYPE_SPACE:
                printf("\\s");
                break;
            case RE_TOK_TYPE_DOT:
                printf(".");
                break;
            default:
                printf("%c", t->c0);
                break;
        }
        t++;
    }
    printf("\n");
}

struct ReToken* tokenize(const char *expr, struct ReToken *buf, size_t size)
{
    struct ReToken *p_out = buf;
    const char **p_in = &expr;
    int i = 0;

    while (strlen(*p_in)) {
        struct ReToken t = re_get_meta_char(p_in);

        assert(t.type != RE_TOK_TYPE_UNDEFINED);
        if (i >= size) {
            ERROR("Max tokensize reached: %ld\n", size);
            return NULL;
        }
        *p_out++ = t;
        i++;
    }
    return buf;
}

struct ReToken* re_rewrite_range(struct ReToken *tokens, size_t size)
{
    /* Extract range from tokens and rewrite to group.
     * This makes it way easier to do the reverse Polish notation algorithm later.
     *
     * eg: [a-zA-Zbx] -> (range_token | range_token | b | x)
     *
     * Algorithm:
     * for TOKEN in TOKENS
     *     TOKEN_TOP_STACK = pop from stack
     *     if TOKEN_TOP_STACK is hyphen
     *         TOKEN_RANGE_START = pop from stack
     *         create TOKEN_RANGE (TOKEN_RANGE_START - TOKEN)
     *         push TOKEN_RANGE to output array
     *     else
     *         push TOKEN to stack
     */
    struct ReToken *t = tokens;
    struct ReToken out_buf[size];
    struct ReToken *t_out = out_buf;

    memset(&out_buf, 0, sizeof(struct ReToken) * size);

    // Temporary buffer for tokens inside cclass
    struct Cclass {
        unsigned char in_cclass;
        struct ReToken *tokens[MAX_CCLASS];
        int size;
    } cclass;

    struct ReToken *stack[size];

    #define STACK_IS_EMPTY() (stackp == stack)
    #define PUSH(S) *stackp++ = S
    #define POP()   *--stackp
    #define RESET_CCLASS() memset(&cclass, 0, sizeof(struct Cclass))
    RESET_CCLASS();

    for (; t->type != RE_TOK_TYPE_UNDEFINED ; t++) {
        if (t->type == RE_TOK_TYPE_CCLASS_START) {
            cclass.in_cclass = 1;
        }
        // Cclass end found. Now convert tokens in temporary buffer to a group so it is easier to do RPN later
        else if (t->type == RE_TOK_TYPE_CCLASS_END) {
            *t_out++ = (struct ReToken){.type=RE_TOK_TYPE_GROUP_START};
            struct ReToken **stackp = stack;
            struct ReToken **cclassp = cclass.tokens;

            for (int i=0 ; i<cclass.size ; i++, cclassp++) {

                struct ReToken *t0 = POP();
                if (STACK_IS_EMPTY() &&  t0->type == RE_TOK_TYPE_HYPHEN) {
                    ERROR("Malformed range\n");
                    return NULL;
                }

                if (t0->type == RE_TOK_TYPE_HYPHEN) {
                    struct ReToken *t1 = POP();
                    *t_out++ = (struct ReToken){.type=RE_TOK_TYPE_RANGE, .c0=t1->c0, .c1=(*cclassp)->c0};

                    if (i > 0)
                        *t_out++ = (struct ReToken){.type=RE_TOK_TYPE_PIPE};
                }
                else {
                    PUSH(t0);
                    PUSH(*cclassp);
                }
            }

            // empty stack into output buffer
            while (!STACK_IS_EMPTY()) {
                *t_out++ = *POP();
                *t_out++ = (struct ReToken){.type=RE_TOK_TYPE_PIPE};
            }

            // remove extra pipe
            t_out--;
            *t_out++ = (struct ReToken){.type=RE_TOK_TYPE_GROUP_END};
            RESET_CCLASS();
        }
        // Add token to temporary cclass buffer
        else if (cclass.in_cclass) {
            cclass.tokens[cclass.size++] = t;
        }
        else {
            *t_out++ = *t;
        }
    }
    memcpy(tokens, out_buf, size*sizeof(struct ReToken));
    return tokens;

    #undef STACK_IS_EMPTY
    #undef PUSH
    #undef POP
    #undef RESET_CCLASS
}

struct ReToken* re_to_explicit_cat(struct ReToken *tokens, size_t size)
{
    /* Put in explicit cat tokens */
    struct ReToken out_buf[size];
    struct ReToken *outp = out_buf;
    struct ReToken *t0 = tokens;

    // return NULL in case of a buffer overflow
    #define PUSH_OR_RETURN(S) if (outp-out_buf >= size) { ERROR("Failed to add concat symbols, buffer is full: %ld\n", size); return NULL; } else { *outp++ = S; }

    memset(&out_buf, 0, sizeof(struct ReToken) * size);

    for (; t0->type != RE_TOK_TYPE_UNDEFINED ; t0++) {

        PUSH_OR_RETURN(*t0);
        struct ReToken *t1 = t0+1;

        if (t1->type != RE_TOK_TYPE_UNDEFINED) {

            if (t0->type != RE_TOK_TYPE_GROUP_START &&
                t1->type != RE_TOK_TYPE_GROUP_END &&
                t1->type != RE_TOK_TYPE_PIPE &&
                t1->type != RE_TOK_TYPE_QUESTION &&
                t1->type != RE_TOK_TYPE_PLUS &&
                t1->type != RE_TOK_TYPE_STAR &&
                t1->type != RE_TOK_TYPE_CARET &&
                t0->type != RE_TOK_TYPE_CARET &&
                t0->type != RE_TOK_TYPE_PIPE) {
                    PUSH_OR_RETURN((struct ReToken){.type=RE_TOK_TYPE_CONCAT});
            }
        }
    }
    memcpy(tokens, out_buf, size*sizeof(struct ReToken));
    return tokens;

    #undef PUSH_OR_RETURN
}

int infix_to_postfix(struct ReToken *tokens, struct ReToken *buf, size_t size)
{
    // USING:
    //   intermediate operator stack
    //   output queue
    //   input array
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

    struct ReToken opstack[MAX_REGEX];    // intermediate storage stack for operators
    memset(opstack, 0, sizeof(struct ReToken) * MAX_REGEX);
    memset(buf, 0, sizeof(struct ReToken) * size);

    struct ReToken *outqp = buf;
    struct ReToken *stackp = opstack;
    struct ReToken op;
    struct ReToken *t = tokens;

    // track how many pipes we've seen and if they're in () or not
    int op_pipe = 0;
    int in_group = 0;

    #define PUSH(S) *stackp++ = S
    #define POP()   *--stackp
    #define PUSH_OUT(S) *outqp++ = S

    while (t->type != RE_TOK_TYPE_UNDEFINED) {
        switch (t->type) {
            case RE_TOK_TYPE_GROUP_START:
                PUSH(*t);
                in_group = 1;
                break;
            case RE_TOK_TYPE_GROUP_END:
                while ((op = POP()).type != RE_TOK_TYPE_GROUP_START)
                    PUSH_OUT(op);
                for (int i=0 ; i<op_pipe ; i++) {
                    struct ReToken t_pipe = {RE_TOK_TYPE_PIPE};
                    PUSH_OUT(t_pipe);
                }
                op_pipe = 0;
                in_group = 0;
                break;
            case RE_TOK_TYPE_PIPE:
                op_pipe++;
                break;
            case RE_TOK_TYPE_CONCAT:
                PUSH(*t);
                break;
            case RE_TOK_TYPE_STAR:
            case RE_TOK_TYPE_PLUS:
            case RE_TOK_TYPE_QUESTION:
                while (stackp != opstack) {
                    op = POP();

                    // check precedence (operators are ordered in enum)
                    if (op.c0 >= t->c0) {
                        PUSH(op);
                        break;
                    }
                    PUSH_OUT(op);
                }
                PUSH_OUT(*t);
                break;
            default:
                PUSH_OUT(*t);
                break;
        }
        t++;
    }

    // empty stack into out queue
    while (stackp != opstack)
        PUSH_OUT(POP());

    // put pipes
    for (int i=0 ; i<op_pipe ; i++) {
        struct ReToken t_pipe = {RE_TOK_TYPE_PIPE};
        PUSH_OUT(t_pipe);
    }

    return 0;

    #undef PUSH
    #undef POP
    #undef PUSH_OUT
}

int re_is_match(struct State *s)
{
    return s->type == STATE_TYPE_MATCH;
}

int re_is_endpoint(struct State *s)
{
    return s->out == NULL && s->out1 == NULL;
}

struct MatchList {
    struct State *states[256];
    int n;
};

struct MatchList re_match_list_init()
{
    struct MatchList l;
    memset(&l, 0, sizeof(struct MatchList));
    l.n = 0;
    return l;

}

void re_match_list_append(struct MatchList *l, struct State *s)
{
    if (s == NULL)
        return;

    if (s->type == STATE_TYPE_SPLIT) {
        DEBUG("SPLIT%d: ", l->n);
        re_match_list_append(l, s->out);
        DEBUG("SPLIT%d: ", l->n);
        re_match_list_append(l, s->out1);
    }
    else {
        //assert("Token should not be NULL!" && s->t != NULL);
        //DEBUG("ADDING STATE: %d, %c\n", s->t->type, s->t->c0);
        l->states[l->n] = s;
        l->n++;
    }
}

void debug_match_list(struct MatchList *l)
{
    struct State *s = *(l->states);
    for (int i=0 ; i<l->n ; i++, s++) {
        if (l->states[i]->type == STATE_TYPE_MATCH) {
            DEBUG("MATCHLIST: [%d] MATCH\n", i);
        }
        else {
            DEBUG("MATCHLIST: [%d] %c\n", i, l->states[i]->t->c0);
            //DEBUG("MATCHLIST: [%d] %c\n", i, s->t->c0);
        }
    }
    DEBUG("\n");
}

struct State* re_match_list_has_match(struct MatchList *l, char c)
{

    /* Return if there is a matching state in the list */
    struct State *s = l->states[0];
    for (int i=0 ; i<l->n ; i++, s++) {
        //if (s->t && s->t->c0 == c) {
        if (l->states[i]->t && l->states[i]->t->c0 == c) {
            DEBUG("FOUND MATCH: '%c'\n", l->states[i]->t->c0);
            return l->states[i];
        }
    }
    DEBUG("NO MATCH: '%c'\n", c);
    return NULL;
}

struct State* re_match_list_has_end(struct MatchList *l)
{
    struct State *s = l->states[0];
    for (int i=0 ; i<l->n ; i++, s++) {
        if (l->states[i]->type == STATE_TYPE_MATCH)
            return s;
    }
    return NULL;
}

int re_match(struct NFA *nfa, const char *str)
{
    const char *c = str;

    // this is where we record the states
    struct MatchList clist = re_match_list_init();
    struct MatchList path = re_match_list_init();

    // add first node
    clist.states[0] = nfa->start;
    clist.n++;
    //re_match_list_append(clist, nfa->start);
    debug_match_list(&clist);

    for (; *c ; c++) {
        DEBUG("CUR CHAR: '%c'\n", *c);

        struct State *match = re_match_list_has_match(&clist, *c);
        if (match) {

            DEBUG("Found match: %c\n", match->t->c0);
            DEBUG("adding match: ");
            re_match_list_append(&path, match);

            clist = re_match_list_init();
            re_match_list_append(&clist, match->out);
            debug_match_list(&clist);

            if (re_match_list_has_end(&clist)) {
                DEBUG("WE GOT A MATCH!!!\n");
                debug_match_list(&path);
                return 1;
            }
        }
        else {
            return -1;
        }
    }

    return 0;

}

int main(int argc, char **argv)
{
    if (argc < 2) {
        ERROR("Missing expression\n");
        return 1;
    }

    if (argc < 3) {
        ERROR("Missing input string\n");
        return 1;
    }

    const char *expr = argv[1];
    const char *input = argv[2];

    DEBUG("Parsing: %s\n", expr);
    struct ReToken tokens_infix[MAX_REGEX]; // regex chars with added concat symbols
    struct ReToken tokens_postfix[MAX_REGEX]; // regex chars with added concat symbols
    
    char *expr_postfix = re2post(expr);
    //if (tokenize(expr_postfix, tokens_infix, MAX_REGEX) == NULL)
    if (tokenize(expr_postfix, tokens_postfix, MAX_REGEX) == NULL)
        return 1;

    DEBUG("TOKENS:  "); debug_reg(tokens_postfix);

    //if (re_rewrite_range(tokens_infix, MAX_REGEX) == NULL)
    //    return 1;


    //DEBUG("RANGE:   "); debug_reg(tokens_infix);
    //
    //if (re_to_explicit_cat(tokens_infix, MAX_REGEX) == NULL)
    //    return 1;

    //DEBUG("CAT:     "); debug_reg(tokens_infix);

    //infix_to_postfix(tokens_infix, tokens_postfix, MAX_REGEX);

    //DEBUG("POSTFIX: "); debug_reg(tokens_postfix);

    struct NFA nfa = nfa_init();
    //struct State *s = nfa_compile_rec(&nfa, tokens_postfix, NULL);
    nfa_compile(&nfa, tokens_postfix);
    state_debug(nfa.start, 0);
    //state_debug(s, 0);
    re_match(&nfa, input);
    return 1;
}
