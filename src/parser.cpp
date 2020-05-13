#include <vili/node.hpp>
#include <vili/types.hpp>

#include <fstream>
#include <iostream>
#include <stack>
#include <tao/pegtl.hpp>
#include <tao/pegtl/contrib/parse_tree.hpp>
#include <tao/pegtl/contrib/trace.hpp>

namespace peg = tao::pegtl;
using namespace std::string_literals;

namespace vili::parser
{
    namespace rules
    {
        // clang-format off
        struct identifier : peg::identifier {};
        struct indent : peg::seq<peg::bol, peg::star<peg::blank>> {};
        struct affectation : peg::seq<identifier, peg::pad<peg::one<':'>, peg::blank>> {};

        // String
        struct string : peg::seq<peg::one<'"'>, peg::star<peg::not_one<'"'>>, peg::one<'"'>> {};

        // Booleans
        struct true_ : peg::string<'t', 'r', 'u', 'e'> {};
        struct false_ : peg::string<'f', 'a', 'l', 's', 'e'> {};
        struct boolean : peg::sor<true_, false_> {};

        // Numbers
        struct sign : peg::one<'+', '-'> {};
        struct integer : peg::seq<peg::opt<sign>, peg::plus<peg::digit>> {};
        struct number : peg::seq<peg::opt<sign>, peg::star<peg::digit>, peg::one<'.'>, peg::plus<peg::digit>> {};

        // Data
        struct data : peg::sor<boolean, number, integer, string> {};

        // Arrays
        struct array;
        struct brace_based_object;
        struct array_element : peg::sor<boolean, number, integer, string, array, brace_based_object> {};
        struct array_elements : peg::list<array_element, peg::one<','>, peg::space> {};
        struct open_array : peg::one<'['> {};
        struct close_array : peg::one<']'> {};
        struct array : peg::seq<open_array, peg::pad_opt<array_elements, peg::space>, close_array> {};

        // Objects
        struct node;
        struct indent_based_object : peg::eol {};
        struct open_object : peg::one<'{'> {};
        struct close_object : peg::one<'}'> {};
        struct comma_or_newline : peg::seq<peg::pad<peg::sor<peg::one<','>, peg::eol>, peg::space>, peg::star<peg::space>> {};
        struct object_elements : peg::list<node, comma_or_newline> {};
        struct brace_based_object : peg::seq<open_object, peg::pad_opt<object_elements, peg::space>, close_object> {};
        struct object : peg::sor<brace_based_object, indent_based_object> {};

        // Comments
        struct inline_comment : peg::seq<peg::one<'#'>, peg::until<peg::eol, peg::any>> {};

        // Nodes
        struct node : peg::seq<affectation, peg::sor<data, array, object>> {};
        struct full_node : peg::seq<indent, node, peg::opt<peg::eol>> {};
        struct empty_line : peg::seq<peg::star<peg::blank>, peg::eol> {};
        struct line : peg::sor<empty_line, inline_comment, full_node> {};
        struct grammar : peg::until<peg::eof, peg::must<line>> {};
        // clang-format on
    }

    class state
    {
    private:
        std::string m_identifier;
        std::stack<std::pair<node*, int>> m_stack;
        int64_t m_indent_base = 4;
        int64_t m_indent_current = -1;

    public:
        node root;
        state();
        void set_indent(int64_t indent);
        void use_indent();
        void set_active_identifier(const std::string& identifier);
        void open_block();
        void close_block();
        void push(node data);
    };

    state::state()
        : root(object {})
    {
        m_stack.push(std::make_pair(&root, 0));
    }

    void state::set_indent(int64_t indent)
    {
        if (m_indent_current == -1 && indent > 0)
        {
            m_indent_base = indent;
        }
        if (indent % m_indent_base && m_stack.top().second)
        {
            throw exceptions::inconsistent_indentation(indent, m_indent_base, EXC_INFO);
        }
        indent /= m_indent_base; // Normalize indentation to "levels"
        if (m_indent_current > indent)
        {
            for (auto decrease_indent = m_indent_current; decrease_indent > indent;
                 decrease_indent--)
            {
                this->close_block();
            }
        }
        else if (m_indent_current == indent && indent < m_stack.top().second)
        {
            this->close_block();
        }
        else if (m_indent_current < indent)
        {
            if (m_indent_current - indent > 1)
            {
                throw exceptions::too_much_indentation(indent, EXC_INFO);
            }
        }
        // std::cout << "================ INDENT FROM " << m_indent_current << " TO " << indent << " / STACK : " << m_stack.size() << std::endl;
        m_indent_current = indent;
    }

    void state::use_indent()
    {
        m_stack.top().second = (m_indent_current + 1);
    }

    void state::set_active_identifier(const std::string& identifier)
    {
        m_identifier = identifier;
    }

    void state::open_block()
    {
        std::cout << "Opening block with identifier " << m_identifier << std::endl;
        m_stack.top().first->insert(m_identifier, node {});
        m_stack.push(std::make_pair(&m_stack.top().first->operator[](m_identifier), 0));
        std::cout << "======================================== STACK OPEN : "
                  << m_stack.size() << std::endl;
    }

    void state::close_block()
    {
        m_stack.pop();
        std::cout << "======================================== STACK EXIT : "
                  << m_stack.size() << std::endl;
    }

    void state::push(node data)
    {
        std::cout << "State::push in " << to_string(m_stack.top().first->type())
                  << std::endl;
        if (m_stack.top().first->is<array>())
        {
            std::cout << "Push in array" << std::endl;
            m_stack.top().first->push(data);
        }
        else if (m_stack.top().first->is<object>())
        {
            std::cout << "Push in object" << std::endl;
            m_stack.top().first->insert(m_identifier, data);
        }
        else
        {
            throw std::runtime_error("Should not happen");
        }
        if (data.is_container())
        {
            m_stack.push(std::make_pair(&m_stack.top().first->back(), 0));
            std::cout << "======================================== STACK OPEN : "
                      << m_stack.size() << std::endl;
            std::cout << "On top of stack is : " << m_stack.top().first->dump()
                      << std::endl;
        }
    }

    template <typename Rule> struct action
    {
    };

    template <> struct action<rules::string>
    {
        template <class ParseInput> static void apply(const ParseInput& in, state& state)
        {
            std::cout << "Found quote : " << in.string() << std::endl;
            auto content = in.string();
            state.push(content.substr(1, content.size() - 2));
        }
    };

    template <> struct action<rules::number>
    {
        template <class ParseInput> static void apply(const ParseInput& in, state& state)
        {
            std::cout << "Found float : " << in.string() << std::endl;
            state.push(std::stod(in.string()));
        }
    };

    template <> struct action<rules::integer>
    {
        template <class ParseInput> static void apply(const ParseInput& in, state& state)
        {
            std::cout << "Found integer : " << in.string() << std::endl;
            state.push(std::stoll(in.string()));
        }
    };

    template <> struct action<rules::boolean>
    {
        template <class ParseInput> static void apply(const ParseInput& in, state& state)
        {
            std::cout << "Found boolean : " << in.string() << std::endl;
            state.push((in.string() == "true" ? true : false));
        }
    };

    /*template <> struct action<rules::affectation>
    {
        template <class ParseInput> static void apply(const ParseInput& in, state& state)
        {
            std::cout << "Found affectation : " << in.string() << std::endl;
            state.open_block();
        }
    };*/

    template <> struct action<rules::identifier>
    {
        template <class ParseInput> static void apply(const ParseInput& in, state& state)
        {
            std::cout << "Found identifier : " << in.string() << std::endl;
            state.set_active_identifier(in.string());
        }
    };

    template <> struct action<rules::open_array>
    {
        template <class ParseInput> static void apply(const ParseInput& in, state& state)
        {
            std::cout << "Opening array : " << in.string() << std::endl;
            state.push(vili::array {});
        }
    };

    template <> struct action<rules::close_array>
    {
        template <class ParseInput> static void apply(const ParseInput& in, state& state)
        {
            std::cout << "Closing array : " << in.string() << std::endl;
            state.close_block();
        }
    };

    template <> struct action<rules::open_object>
    {
        template <class ParseInput> static void apply(const ParseInput& in, state& state)
        {
            std::cout << "Opening object : " << in.string() << std::endl;
            state.push(vili::object {});
        }
    };

    template <> struct action<rules::close_object>
    {
        template <class ParseInput> static void apply(const ParseInput& in, state& state)
        {
            std::cout << "Closing object : " << in.string() << std::endl;
            state.close_block();
        }
    };

    template <> struct action<rules::indent_based_object>
    {
        template <class ParseInput> static void apply(const ParseInput& in, state& state)
        {
            std::cout << "Opening [via indent] object : " << in.string() << std::endl;
            state.push(vili::object {});
            state.use_indent();
        }
    };

    template <> struct action<rules::indent>
    {
        template <class ParseInput> static void apply(const ParseInput& in, state& state)
        {
            std::cout << "Set indent level to " << in.string().size() << std::endl;
            state.set_indent(in.string().size());
        }
    };

    vili::node from_string(std::string_view data)
    {
        state parser_state;
        peg::string_input in(data, "string_source");
        std::cout << "Parsing : " << data << std::endl;

        try
        {
            peg::parse<vili::parser::rules::grammar, vili::parser::action>(
                in, parser_state);
            /*peg::standard_trace<vili::parser::rules::grammar, vili::parser::action>(
                in, parser_state);*/
        }
        catch (peg::parse_error& e)
        {
            const auto p = e.positions.front();
            std::cerr << e.what() << '\n'
                      << in.line_at(p) << '\n'
                      << std::setw(p.byte_in_line) << ' ' << '^' << std::endl;
        }
        /*catch (vili::exceptions::base_exception& e)
        {
            std::cerr << "vili::exception : " << e.what() << std::endl;
        }*/

        return parser_state.root;
    }

    vili::node from_file(std::string_view path)
    {
        // TODO: File not found exception
        std::ifstream input_file;
        input_file.open(path.data());
        std::stringstream buffer;
        buffer << input_file.rdbuf();
        const std::string content = buffer.str();
        return from_string(content);
    }
}