#!/usr/bin/env bash

# Initialize client angular sub project
pnpm create @angular client --standalone --style=css --routing

# ✔ Do you want to enable Server-Side Rendering (SSR) and Static Site Generation (SSG/Prerendering)? No
# ✔ Do you want to create a 'zoneless' application without zone.js? Yes
# ✔ Which AI tools do you want to configure with Angular best practices? https://angular.dev/ai/develop-with-ai None
# CREATE client/README.md (1469 bytes)
# CREATE client/.editorconfig (314 bytes)
# CREATE client/.gitignore (587 bytes)
# CREATE client/angular.json (2276 bytes)
# CREATE client/package.json (1103 bytes)
# CREATE client/tsconfig.json (992 bytes)
# CREATE client/tsconfig.app.json (429 bytes)
# CREATE client/tsconfig.spec.json (408 bytes)
# CREATE client/.vscode/extensions.json (130 bytes)
# CREATE client/.vscode/launch.json (470 bytes)
# CREATE client/.vscode/tasks.json (938 bytes)
# CREATE client/src/main.ts (222 bytes)
# CREATE client/src/index.html (292 bytes)
# CREATE client/src/styles.css (80 bytes)
# CREATE client/src/app/app.css (0 bytes)
# CREATE client/src/app/app.spec.ts (780 bytes)
# CREATE client/src/app/app.ts (288 bytes)
# CREATE client/src/app/app.html (20122 bytes)
# CREATE client/src/app/app.config.ts (383 bytes)
# CREATE client/src/app/app.routes.ts (77 bytes)
# CREATE client/public/favicon.ico (15086 bytes)
# ✔ Packages installed successfully.
# hint: Using 'master' as the name for the initial branch. This default branch name
# hint: is subject to change. To configure the initial branch name to use in all
# hint: of your new repositories, which will suppress this warning, call:
# hint:
# hint:   git config --global init.defaultBranch <name>
# hint:
# hint: Names commonly chosen instead of 'master' are 'main', 'trunk' and
# hint: 'development'. The just-created branch can be renamed via this command:
# hint:
# hint:   git branch -m <name>
#     Successfully initialized git.

# Initialize server nestjs sub project
pnpm i -g @nestjs/cli
nest new server
# ✨  We will scaffold your app in a few seconds..
#
# ? Which package manager would you ❤️  to use?
#   npm
#   yarn
# ❯ pnpm
# CREATE server/.prettierrc (51 bytes)
# CREATE server/README.md (5036 bytes)
# CREATE server/eslint.config.mjs (836 bytes)
# CREATE server/nest-cli.json (171 bytes)
# CREATE server/package.json (1977 bytes)
# CREATE server/tsconfig.build.json (97 bytes)
# CREATE server/tsconfig.json (677 bytes)
# CREATE server/src/app.controller.ts (274 bytes)
# CREATE server/src/app.module.ts (249 bytes)
# CREATE server/src/app.service.ts (142 bytes)
# CREATE server/src/main.ts (228 bytes)
# CREATE server/src/app.controller.spec.ts (617 bytes)
# CREATE server/test/jest-e2e.json (183 bytes)
# CREATE server/test/app.e2e-spec.ts (674 bytes)
#
# ✔ Installation in progress... ☕
#
# 🚀  Successfully created project server
# 👉  Get started with the following commands:
#
# $ cd server
# $ pnpm run start
#
#
#                           Thanks for installing Nest 🙏
#                  Please consider donating to our open collective
#                         to help us maintain this package.
#
#
#                🍷  Donate: https://opencollective.com/nest

# Setup angular cli tool
pnpm install -g @angular/cli

# Setup CoreUI
cd client
ng add @coreui/angular
# ✔ Determining Package Manager
#   › Using package manager: pnpm
# ✔ Searching for compatible package version
#   › Found compatible package version: @coreui/angular@5.5.12.
# ✔ Loading package information from registry
# ✔ Confirming installation
# ✔ Installing package
#
#     Installing @coreui/angular dependencies...
#     @angular/core version ^20.3.0
#     Added dependency: @angular/animations@^20.3.0
#     Added dependency: @angular/common@^20.3.0
#     Added dependency: @angular/core@^20.3.0
#     Added dependency: @angular/router@^20.3.0
#     Added dependency: @angular/cdk@^20.2.0
#     Added dependency: @coreui/coreui@~5.4.3
#     Added dependency: @coreui/icons-angular@~5.5.12
#     Added dependency: @popperjs/core@~2.11.8
#     Installing @coreui/angular@~5.5.12
# UPDATE package.json (1386 bytes)
# ✔ Packages installed successfully.
