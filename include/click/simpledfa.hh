// -*- c-basic-offset: 4 -*-
#ifndef CLICK_SIMPLEDFA_HH
#define CLICK_SIMPLEDFA_HH

#include <click/string.hh>
CLICK_DECLS

/**
 * Greedy DFA, creates as many patterns as there are letters in the patterns. Limited to 65536 characters.
 */
class SimpleDFA {

public:
    const static int _verbose = 0;
    typedef uint16_t state_t;

    struct NextState {
        state_t next[256];
    };

    static const uint16_t MATCHED = (uint16_t)-1;

    SimpleDFA() : vector() {
        vector.resize(1);
    }

    typedef NextState StateSet;

    Vector<StateSet> compile(String pattern) {
        int state = 0;
        Vector<StateSet> c_pattern;
        for (int i = 0; i < pattern.length(); i++) {
            if (c_pattern.size() <= state)
                c_pattern.resize(state + 1);
            int next_state;
            if (i + 1 == pattern.length())
                next_state = MATCHED;
            else
                next_state = state + 1;
            if (pattern[i] == '\\') {
                if (pattern[i + 1] == '\\') {
                    i = i + 1;
                } else {
                    i = i + 1; //Go to the next one and interpret normally
                }
            } else if (pattern[i] == '*') { //unescaped
                for (int j = 0; j < 256; j ++) {
                    if (next_state == MATCHED)
                        c_pattern[state].next[j] = MATCHED;
                    else
                        c_pattern[state].next[j] = state;
                }
                //Do not set state = next_state, we will overwrite
                continue;
            }
            c_pattern[state].next[pattern[i]] = next_state;
            state = next_state;
        }
        return c_pattern;


    }

    int add_pattern(String pattern) {
        int prevsz = vector.size();

        if (_verbose)
            click_chatter("Adding pattern %s",pattern.c_str());
        Vector<StateSet> c_pattern = compile(pattern);
        if (_verbose) {
            click_chatter("Compiled :");
            print(c_pattern);
        }
        int next_state;
        if (c_pattern.size() > 1) {
            create_states(c_pattern,prevsz); // Will add all the states at the end of the vector
            next_state = prevsz - 1;
        } else {
            next_state = MATCHED;
        }
        for (int j = 0; j < prevsz; j++) { //We now attach the first letter to each possible state
            attach(c_pattern,j, c_pattern[0], next_state);
        }
        if (_verbose) {
            click_chatter("Final:");
            print(vector);
        }
        return 0;
    }

    void attach(Vector<StateSet> c_pattern, state_t state, NextState nt, int pos) {
        for (int j = 0; j < 256; j++) {
            state_t p = nt.next[j];
            if (p) {
                state_t n = vector[state].next[j];
                if (n == MATCHED)
                    continue;
                if (p == MATCHED) {
                    vector[state].next[j] = MATCHED;
                    continue;
                }
                if (n) {
                    //A T
                    //A *-> 4
                    //click_chatter("%c exists, attaching state %d, to state %d",pattern[l],vector[state].next[pattern[l]],vector[next_state].next[pattern[l + 1]]);
                    //TODO : does not work here if c_pattern is a glob
                    attach(c_pattern, n, c_pattern[nt.next[j]], pos);
                } else
                    vector[state].next[j] = nt.next[j] + pos;
            }
        }
    }

    void create_states(Vector<StateSet> c_pattern, int place) {
        int state = place;
        for (int i = 1; i < c_pattern.size(); i++) {
            if (vector.size() <= state)
                vector.resize(state + 1);
            for (int j = 0; j < 256; j++) {
                if (c_pattern[i].next[j]) {
                    if (c_pattern[i].next[j] == MATCHED) {
                        vector[state].next[j] = MATCHED;
                    } else {
                        vector[state].next[j] = place - 1 + c_pattern[i].next[j];
                    }
                }
            }
            ++state;
        }
    }

    inline void next(const unsigned char& c, state_t &state) {
        state = vector[state].next[c];
    }

    inline void next_chunk(const unsigned char*& c, const int& size, state_t &state) {
        for (int i = 0; i < size; i++) {
            state = vector[state].next[*(c + i)];
            if (state == MATCHED)
                return;
        }
    }

    void print(Vector<NextState> v) {
        for (int i = 0; i < v.size(); i ++) {
            click_chatter("S%d",i);
            for (int j = 0 ; j < 256; j ++) {
                if (v[i].next[j])
                    click_chatter("  %d - %c : %d",j,(char)j, v[i].next[j]);
            }
        }
    }
    /** Adding attack
     *   0 1 2 3 4 5
     * A 1     4
     * C 0       5
     * K 0         -1
     * T 0 2 3
     *
     * Now adding bomb
     *   0 1 2 3 4 5
     * A 1     4
     * C 0       5
     * K 0        -1
     * T 0 2 3
     * B 6             -1
     * O              8
     * M                9
     *
     */
private:
    Vector<NextState> vector;

};

CLICK_ENDDECLS

#endif
