// TODO
// - Support function call expressions
// - Optimize caching and evaluation algorithm for cases where all the 
//   children of a parent have the same set of inputs.  In this case, the 
//   children don't need to be cached because it will never be used.  This is 
//   because any change in a child causes all children to be re-evaluated.
// - Decide whether to keep the lambda method of storing the expression object,
//   or choose some other option:
//    - Give memoize<> template a non-template virtual interface.  Similar 
//      runtime overhead as now (only one late-bound function call per 
//      top-level expression evaluation), but maybe more memory for virtual 
//      function table?
//    - Use a non-template cache data structure that can be generated from an 
//      expression object.  Compiler maybe can't optimize this as much.
//    - Find some way to avoid type-erasure when storing the expression object.
//      This seems to require de-coupling the input from the expression in 
//      order to use decltype() when declaring class members.  This is not 
//      good, because it requires an extra step to bind the input data to the 
//      rendering expression.

#include "stdafx.h"

#include <boost/proto/proto.hpp>
#include <functional>
#include <vector>

namespace proto = boost::proto;
namespace mpl = boost::mpl;
namespace fusion = boost::fusion;

namespace memoize
{

    template <typename Expr> struct memoize;
    struct eval_cache_context;

    // This is a wrapper class that allows a some object to be used as input to a 
    // memoized expression.  The type T must be DefaultConstructible, 
    // EqualityComparable and Copyable.  Use in() for convenience.
    template <typename T>
    struct input
    {
        T& src;
        mutable T cache;

        input(T& source) : src(source), cache()
        {
        }
    };

    template <typename T>
    std::ostream& operator<<(std::ostream& s, const input<T>& i)
    {
        s << "input";
        return s;
    }

    template <typename T>
    input<T> in(T& t) { return input<T>(t); }

    struct memoize_domain
        : proto::domain < proto::generator<memoize> >
    {
        // The memoize domain customizes as_child so that expressions are held by 
        // value.  This allows expression objects to be passed around or stored as
        // class member data.
        template <typename T>
        struct as_child
            : proto_base_domain::as_expr < T >
        {
        };
    };

    template <typename Expr>
    struct memoize
        : proto::extends < Expr, memoize<Expr>, memoize_domain >
    {
        typedef proto::extends<Expr, memoize<Expr>, memoize_domain> base_type;
        typedef typename proto::result_of::eval<memoize<Expr>, eval_cache_context const>::type cache_type;

        memoize(Expr const& expr = Expr()) : base_type(expr), dirty(true) {}

        mutable cache_type result;

        // Fix me: This flag is only meaningful for non-terminals. Terminal 
        // dirtiness is determined by operator== on the source data.  I think a 
        // custom generator could be used to provide an alternate memoize 
        // implementation for terminals.
        mutable bool dirty;
    };

    template <typename T>
    struct is_terminal : mpl::false_ {};

    template <typename T>
    struct is_terminal<input<T> > : mpl::true_{};

    BOOST_PROTO_DEFINE_OPERATORS(is_terminal, memoize_domain);

    // This context marks dirty all sub-expressions who depend on terminals 
    // that are dirty.
    struct mark_dirty_context
    {
        template <
            typename Expr,
            typename Tag = typename proto::tag_of<Expr>::type>
        struct eval
            : proto::default_eval < Expr, mark_dirty_context const >
        {
            typedef bool result_type;

            result_type operator()(Expr& e, mark_dirty_context const& ctx)
            {
                // If it has already been marked dirty (this condition ocurrs normally 
                // before any evaluation has taken place), no need to look further.
                if (e.dirty) return e.dirty;

                // Mark child expressions, and if any are dirty mark this expression as 
                // dirty too.
                return e.dirty = fusion::fold(e, false,
                    std::bind(std::logical_or<bool>(), std::placeholders::_1,
                    std::bind(proto::functional::eval(), std::placeholders::_2, ctx)));
            }
        };

        template <typename Expr>
        struct eval < Expr, proto::tag::terminal >
        {
            typedef bool result_type;

            result_type operator()(Expr& e, mark_dirty_context const&)
            {
                auto& value = proto::value(e);
                return e.dirty = !(value.cache == value.src);
            }
        };
    };

    // This context evalutes an expression by re-evaluating any sub-expressions 
    // that are dirty, and returning the cached result.
    struct eval_cache_context
    {
        template <
            typename Expr,
            typename Tag = typename proto::tag_of<Expr>::type>
        struct eval
            : proto::default_eval < Expr, eval_cache_context const >
        {
            typedef proto::default_eval<Expr, eval_cache_context const> base_type;

            typename base_type::result_type operator()(Expr& e, eval_cache_context const& ctx)
            {
                if (e.dirty)
                {
                    e.result = base_type::operator()(e, ctx);
                    e.dirty = false;
                }
                return e.result;
            }
        };

        template <
            typename Expr,
            typename Value = typename proto::result_of::value<Expr>::type>
        struct eval_terminal;

        template <typename Expr, typename T>
        struct eval_terminal < Expr, input<T> >
        {
            typedef T result_type;

            result_type& operator()(Expr& e, eval_cache_context const&)
            {
                auto& value = proto::value(e);
                value.cache = value.src;
                e.dirty = false;
                return value.cache;
            }
        };

        template <typename Expr>
        struct eval <Expr, proto::tag::terminal>
            : eval_terminal < Expr >
        {
        };
    };

    template <typename Expr>
    typename proto::result_of::eval<Expr, eval_cache_context const>::type
        reevaluate(memoize<Expr> const& e)
    {
        proto::eval(e, mark_dirty_context());
        return proto::eval(e, eval_cache_context());
    }

    struct renderer
    {
        std::function<void()> _f;

        template <typename Expr>
        renderer& operator=(Expr& e)
        {
            proto::display_expr(e);
            _f = [=]() { reevaluate(e); };
            return *this;
        }

        void operator()()
        {
            if (_f) _f();
        }
    };

    struct ui_element
    {
        int i1, i2, i3;
        renderer _renderer;

        ui_element()
        {
            _renderer = (in(i1) + in(i2) + in(i3));
            i1 = 1;
            i2 = 11;
            i3 = 111;
        }

        void render() { _renderer(); }
    };
}

int main(int argc, char* argv[])
{
    int a, b, c;

    proto::display_expr(proto::as_expr(memoize::in(a))(1));

    memoize::ui_element e;

    e.render();
    e.render();

    e.i2 += 5;

    e.render();

    return 0;
}

