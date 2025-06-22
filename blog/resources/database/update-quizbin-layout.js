db = db.getSiblingDB("quizbin");

var qbHeaderValue = `<!DOCTYPE html>
<html lang="en">
  <head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>%REPLACE_WITH_TITLE_ID%</title>

    <!-- SEO Meta Tags -->
    <meta name="description" content="QuizBin">
    <meta name="keywords" content="QuizBin">
    <meta name="author" content="Randolph Ledesma">
    <meta name="robots" content="index, follow">

    <meta name="theme-color" content="#ffffff">
    <meta http-equiv="X-UA-Compatible" content="IE=edge">

    <link rel="stylesheet" href="https://assets.quizbin.com/normalize.css">
  </head>
  <body>`;

db.layouts.deleteOne({ id: 1 });
db.layouts.insertMany([
  { id: 1, header: qbHeaderValue, footer: "</body></html>" },
]);
