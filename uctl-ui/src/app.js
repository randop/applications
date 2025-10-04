// App module depends on VanJS
define(["van"], function (van) {
  const log = console;

  const { div, p, ul, li, a, span, button } = van.tags;

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
    vanJs.add(mainContainer, Hello());
    vanJs.add(mainContainer, Counter());
  };

  return { init };
});
