// App module depends on VanJS
define(["van"], function (van) {
  const log = console;

  const {
    article,
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
    select,
    option,
    details,
    summary,
  } = van.tags;

  const LoginPanel = () => {
    return article(
      { id: "login-panel" },
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

  const MqttPanel = () => {
    return article(
      { id: "mqtt-panel" },
      h3("MQTT"),
      p("Enter MQTT connection details"),
      form(
        { method: "POST" },
        label({ for: "mqtt-protocol" }, "Protocol"),
        select({ id: "mqtt-protocol" }, option({}, "mqtt://")),
        label({ for: "mqtt-host" }, "Host"),
        input({
          id: "mqtt-host",
          type: "text",
          placeholder: "Host name or IP address",
        }),
        details(
          summary("Advanced options"),
          fieldset(
            label({ for: "mqtt-port" }, "Port"),
            input({ id: "mqtt-port", type: "text", placeholder: "Port" }),
          ),
        ),
      ),
    );
  };

  const init = (vanJs) => {
    const mainContainer = document.querySelector("main.container");
    vanJs.add(mainContainer, LoginPanel());
    vanJs.add(mainContainer, MqttPanel());
  };

  return { init };
});
