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
#define MAX_PLIST_POOL  1024
#define MAX_GROUP_STACK 256
#define MAX_STATE_OUT   1024



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

struct Group {
    struct State *start;
    struct State **out[MAX_STATE_OUT];
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
    const int spaces = 3;
    for (int i=0 ; i<level*spaces ; i++)
        printf(" ");

    if (s == NULL) {
        printf("-> NULL\n");
        return;
    }

    char buf[16] = "";
    switch (s->c) {
        case STATE_MATCH:
            strcpy(buf, "MATCH");
            break;
        case STATE_SPLIT:
            strcpy(buf, "SPLIT");
            break;
        default:
            sprintf(buf, "\'%c\'", s->c);
            break;

    }
    printf("-> State %s\n", buf);
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

struct Group group_init(struct NFA *nfa, struct State *s_start, struct State **s_out)
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

    // initialize out array with one item
    for (int i=0 ; i<MAX_STATE_OUT ; i++)
        g.out[i] = NULL;

    g.out[0] = s_out;
    g.start = s_start;
    return g;
}

void group_patch_out(struct Group *g, struct State *s)
{
    /* Make all states in out list point to s */
    for (int i=0 ; i<MAX_STATE_OUT ; i++) {
        if (g->out[i] == NULL)
            break;
        *(g->out[i]) = s;
    }
}

struct Group* group_join(struct Group *g0, struct Group *g1)
{
    struct State **s = g0->out[0];
    while (*(s+1) != NULL)
        s++;

    
}

void group_copy_out_list(struct Group *g, struct State **states, size_t len)
{
    memcpy(g->out, states, len);
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
    struct Group *stackp = stack;
    struct Group g0, g1; // the paths groups take from stack
    struct Group g;      // the new path after CONCAT or SPLIT

    struct State *s;

    #define PUSH(S) *stackp++ = S
    #define POP()   *--stackp

    DEBUG("Compiling pattern: '%s'\n", pattern);

    for (const char *p=pattern ; *p ; p++) {
        DEBUG("CUR CHAR: %c\n", *p);
        switch (*p) {
            case '&':       // concat
                g1 = POP();
                g0 = POP();
                group_patch_out(&g0, g1.start);
                g = group_init(nfa, g0.start, NULL);
                // replace out list
                memcpy(g.out, g1.out, MAX_STATE_OUT);
                PUSH(g);
                break;
            case '?':       // zero or one
                break;
            case '|':       // alternate
                g1 = POP();
                g0 = POP();
                s = nfa_state_init(nfa, STATE_SPLIT, g0.start, g1.start);
                break;
            case '*':       // zero or more
                g = POP();
                s = nfa_state_init(nfa, STATE_SPLIT, g.start, NULL);
                group_patch_out(&g, s);
                PUSH(group_init(nfa, s, &s->out1));
                break;
            case '+':       // one or more
                g = POP();
                s = nfa_state_init(nfa, STATE_SPLIT, g.start, NULL);
                group_patch_out(&g, s);
                PUSH(group_init(nfa, g.start, &s->out1));
                break;
            default:        // it is a normal character
                s = nfa_state_init(nfa, *p, NULL, NULL);
                PUSH(group_init(nfa, s, &s->out));
                break;
        }
    }

    stack_debug(stack, MAX_GROUP_STACK);


    g = POP();

    // connect last state that indicates a succesfull match
    struct State *match_state = nfa_state_init(nfa, STATE_MATCH, NULL, NULL);


    group_patch_out(&g, match_state);



    state_debug(g.start, 0);
    return g.start;
}

int main()
{
    struct NFA nfa = nfa_init();
    nfa_compile(&nfa, "ab*a+cd&b&");
    return 1;




}
