INSERT INTO
    layouts (
        id,
        created_at,
        updated_at,
        "name",
        "header",
        footer
    )
VALUES
    (
        1,
        NOW (),
        NOW (),
        'default',
        '<!DOCTYPE html>
    <html>
      <head>
        <meta charset="utf-8" />
        <title>%REPLACE_WITH_TITLE_ID%</title>
      </head>
      <style>
        html,
        body {
          background: #fff;
          color: #000;
          font-family: "Source Sans Pro", sans-serif;
        }

        h1 {
          font-size: 3rem;
          line-height: 4rem;
          margin-bottom: 1rem;
        }

        hr {
          border: none;
          height: 1px;
        }

        @keyframes load {
          0% {
            opacity: 0;
          }

          100% {
            padding-left: 4rem;
            opacity: 1;
          }
        }
      </style>
      <body>',
        '</body></html>'
    );
