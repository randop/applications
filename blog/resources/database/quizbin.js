// Connect to quizbin database and initialize
db = db.getSiblingDB("quizbin");

//*****************************************************************************
// Create modes
//*****************************************************************************
db.createCollection("modes");
// Create a unique index on the 'id' field
db.modes.createIndex({ id: 1 }, { unique: true });
db.modes.insertMany([
  { id: 1, mode: "markdown" },
  { id: 2, mode: "html" },
  { id: 3, mode: "plain" },
]);

//*****************************************************************************
// Create layouts
//*****************************************************************************
db.createCollection("layouts");
const defaultHeaderValue = `<!DOCTYPE html>
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
  </head>
  <body>`;
// Create a unique index on the 'id' field
db.layouts.createIndex({ id: 1 }, { unique: true });
db.layouts.insertMany([
  { id: 1, header: defaultHeaderValue, footer: "</body></html>" },
]);

//*****************************************************************************
// Create pages
//*****************************************************************************
db.createCollection("pages");
// Create a unique index on the 'id' field
db.pages.createIndex({ id: 1 }, { unique: true });
const indexPageContent = `
<h1>QuizBin</h1>
<p>Welcome to QuizBin blog</p>
`;
const aboutPageContent = `
> TODO
`;
db.pages.insertMany([
  {
    id: "index",
    createdAt: new ISODate("2025-06-22T23:02:00Z"),
    updatedAt: new ISODate("2025-06-22T23:02:00Z"),
    modeId: 2,
    layoutId: 1,
    title: "QuizBin",
    content: indexPageContent,
  },
  {
    id: "about",
    createdAt: new ISODate("2025-06-22T23:02:00Z"),
    updatedAt: new ISODate("2025-06-22T23:02:00Z"),
    modeId: 1,
    layoutId: 1,
    title: "About",
    content: aboutPageContent,
  },
]);
