// Connect to the database specified by MONGO_INITDB_DATABASE
db = db.getSiblingDB("localhost");

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
    <meta name="description" content="Randolph Ledesma">
    <meta name="keywords" content="Randolph Ledesma">
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
<h1>Hello World</h1>
<p>Lorem ipsum dolor sit amet, consectetur adipiscing elit. Nulla ornare suscipit justo, vel tempor ligula pellentesque ac. Sed at justo lacinia, mollis urna viverra, tincidunt est. Nunc vehicula aliquam eros eu ullamcorper. Aenean placerat dui at posuere dictum. Fusce nec massa eu urna accumsan feugiat a in dolor. Suspendisse nulla elit, aliquet in nibh nec, iaculis tempor leo. Proin ipsum libero, dictum sed placerat quis, ultricies sed ante. Nullam luctus, neque at laoreet cursus, nulla ante egestas lacus, vitae rutrum orci risus sit amet lacus.</p>
<p>Etiam nibh massa, ornare vitae tristique et, tristique a lacus. Integer ac molestie erat. Donec sit amet massa dui. Proin sit amet sollicitudin ligula. Cras pretium auctor dui, sed consequat urna egestas sed. Vestibulum volutpat lorem quis tellus commodo auctor. Vivamus gravida commodo odio et mollis. Sed euismod interdum leo, bibendum volutpat sapien elementum at.</p>
`;
const aboutPageContent = `
Lorem ipsum dolor sit amet, consectetur adipiscing elit. Curabitur eu ultrices ligula. Maecenas id diam a elit viverra varius non quis ipsum. Donec interdum erat massa, in bibendum enim dignissim sed. Praesent viverra egestas ex id semper. Etiam molestie metus justo. Interdum et malesuada fames ac ante ipsum primis in faucibus. Phasellus maximus purus eget consequat pulvinar. Curabitur volutpat metus in erat vestibulum, vitae sollicitudin velit hendrerit. Pellentesque suscipit iaculis felis, nec volutpat enim commodo in. Aenean eu vehicula tellus, eget ullamcorper nisl. Phasellus ut arcu ac justo interdum ultricies. Duis auctor mauris in aliquet vehicula. Fusce sit amet mi diam. Vestibulum turpis velit, ultrices et luctus vel, interdum in risus. Integer metus metus, pretium nec eros maximus, laoreet tincidunt nisl. Donec eu rutrum lorem.

Vestibulum tortor odio, congue fringilla magna in, convallis molestie dui. Donec quis erat egestas, aliquet neque in, dignissim quam. Proin semper tellus at vestibulum euismod. Duis sollicitudin malesuada libero sed cursus. Fusce eget justo dolor. Etiam sodales tellus id metus ullamcorper, non molestie elit porta. Mauris aliquet sagittis sodales. Mauris vitae varius turpis. Proin hendrerit egestas pellentesque. Cras fermentum et purus in placerat. Phasellus vestibulum elit laoreet nibh tristique, non faucibus erat ultricies. Pellentesque ut fringilla sapien. Cras maximus malesuada quam quis blandit. Donec hendrerit molestie diam et consequat.

\`\`\`cpp
#include <iostream>

int main() {
    std::cout << "Hello World!!!" << std::endl;
    return 0;
}
\`\`\`

> Nulla rhoncus turpis sed purus mollis posuere. Aliquam erat volutpat. Morbi tincidunt arcu est, sit amet placerat nibh vehicula eu. Nunc eu egestas velit, eget imperdiet sem. Maecenas sed eleifend leo, sed eleifend massa. Nullam auctor ante sed diam sagittis pretium. Aliquam et sem sapien. Nulla congue risus id consequat posuere. Donec non lectus vel lorem tincidunt facilisis. Quisque leo magna, suscipit consectetur mollis eget, commodo eu dui. Phasellus feugiat malesuada diam, at elementum nunc tempus ut. Nulla tellus nunc, interdum nec efficitur ut, finibus eu mi.
`;
db.pages.insertMany([
  {
    id: "index",
    createdAt: new ISODate("2025-06-22T23:02:00Z"),
    updatedAt: new ISODate("2025-06-22T23:02:00Z"),
    modeId: 2,
    layoutId: 1,
    title: "hello",
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
