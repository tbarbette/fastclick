#ifndef CLICK_TINYEXPR_HH
#define CLICK_TINYEXPR_HH

#include <tinyexpr/tinyexpr.h>

CLICK_DECLS

static double square_wave(double x) {
        if ((int)(x * 2) % 2 < 1)
            return 1;
        else
            return -1;
}

static double t_min(double a, double b) {
        if (a < b)
            return a;
        else
            return b;
}

static double t_max(double a, double b) {
        if (a > b)
            return a;
        else
            return b;
}



class TinyExpr { public:

    static TinyExpr compile(String expr, int n_vars) {

        static constexpr const char* const var_names[] = {"x", "y", "z"};
        TinyExpr e;
        assert(n_vars < 4);
        e._n_vars = n_vars + 3;
        e._vars = (te_variable*)CLICK_LALLOC(sizeof(te_variable) * e._n_vars);
        e._vars_values = (double*)CLICK_LALLOC(sizeof(double) * e._n_vars);
        for (int i = 0; i < n_vars; i++) {
            e._vars[i] = {var_names[i], &e._vars_values[i], 0};
        }
        e._vars[n_vars] = (te_variable){"squarewave", (const void*)square_wave, TE_FUNCTION1};
        e._vars[n_vars + 1] = (te_variable){"min", (const void*)t_min, TE_FUNCTION2};
        e._vars[n_vars + 2] = (te_variable){"max", (const void*)t_max, TE_FUNCTION2};
        int error;
        e._expr = te_compile(expr.c_str(), e._vars, e._n_vars, &error); 
        assert(error == 0);
        return e;
    }

    double eval(double x) {
        _vars_values[0] = x;
        return te_eval(_expr);
    }

    double eval() {
        return te_eval(_expr);
    }

    TinyExpr(const TinyExpr& expr) {
        if (_expr) {
            if (_expr == expr._expr)
                return;
            this->~TinyExpr();
        }
        _expr = expr._expr;
        _n_vars = expr._n_vars;
        _vars = expr._vars;
        _vars_values = expr._vars_values;
        _use_count = expr._use_count;
        _use_count = _use_count + 1;
    }

    operator bool() const {
        return _expr;
    }

    ~TinyExpr() {
        if (_expr && --*_use_count == 0) {
            te_free(_expr);
            CLICK_LFREE(_vars,  _n_vars);
            CLICK_LFREE(_vars_values,  _n_vars);
            CLICK_LFREE(_use_count, sizeof(unsigned));
        }
    }

    TinyExpr() : _expr(0) {
        _use_count = (unsigned*)CLICK_LALLOC(sizeof(unsigned));
    }
private:
    te_expr *_expr;
    te_variable *_vars;
    double* _vars_values;
    int _n_vars;
    unsigned *_use_count;
};

CLICK_ENDDECLS

#endif 
