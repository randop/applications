// App module depends on VanJS
define(["van"], function (van) {
  const log = console;

  const {
    div,
    p,
    ul,
    li,
    a,
    span,
    button,
    section,
    h3,
    form,
    input,
    fieldset,
    label,
  } = van.tags;

  const Hello = () =>
    div(
      p("👋Hello"),
      ul(
        li("🗺️World"),
        li(
          a(
            { href: "https://randop.github.io/" },
            "🍦 https://randop.github.io",
          ),
        ),
      ),
    );

  const Login = () => {
    return section(
      { id: "login" },
      h3("Login"),
      p("Enter user and password"),
      form(
        { method: "POST" },
        input({
          type: "text",
          name: "user",
          placeholder: "user",
          required: true,
          "aria-label": "user",
        }),
        input({
          type: "password",
          name: "password",
          placeholder: "password",
          "aria-label": "password",
          required: true,
        }),
        button({ type: "submit" }, "Login"),
        fieldset(
          label(
            { for: "terms" },
            input({
              type: "checkbox",
              role: "switch",
              id: "terms",
              name: "terms",
            }),
            " I agree to the ",
            a({ href: "#" }, "Privacy Policy"),
          ),
        ),
      ),
    );
  };

  const Counter = () => {
    const counter = van.state(0);
    return span(
      "❤️ ",
      counter,
      " ",
      button({ onclick: () => ++counter.val }, "👍"),
      " ",
      button({ onclick: () => --counter.val }, "👎"),
    );
  };

  const init = (vanJs) => {
    const mainContainer = document.querySelector("main.container");
    vanJs.add(mainContainer, Login());
    vanJs.add(mainContainer, Hello());
    vanJs.add(mainContainer, Counter());
  };

  return { init };
});
