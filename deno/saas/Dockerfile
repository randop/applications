# Stage 1: Build the application
FROM node:22-alpine AS builder

WORKDIR /app

# Enable pnpm via corepack
RUN corepack enable

# Set CI mode for non-interactive pnpm commands
# Fixes errors such as:
# ERR_PNPM_ABORTED_REMOVE_MODULES_DIR_NO_TTY  
# Aborted removal of modules directory due to no TTY
ENV CI=true

COPY package.json pnpm-lock.yaml ./

RUN pnpm install --frozen-lockfile --dangerously-allow-all-builds

COPY . .

RUN pnpm run build

# Prune dev dependencies (keep only production)
RUN pnpm prune --prod

# Stage 2: Production runtime image
FROM gcr.io/distroless/nodejs22-debian12:nonroot

WORKDIR /app

COPY --from=builder --chown=nonroot:nonroot /app/node_modules ./node_modules
COPY --from=builder --chown=nonroot:nonroot /app/dist ./dist

EXPOSE 8080

CMD ["dist/main.js"]
