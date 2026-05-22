# ── Stage 1: 构建 ──────────────────────────────────────────
FROM node:20-alpine AS builder

ARG VITE_AMAP_KEY
ARG VITE_AMAP_SECURITY_KEY
ENV VITE_AMAP_KEY=$VITE_AMAP_KEY
ENV VITE_AMAP_SECURITY_KEY=$VITE_AMAP_SECURITY_KEY

WORKDIR /app

COPY package*.json ./
RUN npm ci

COPY . .
RUN npm run build

# ── Stage 2: 生产运行 ──────────────────────────────────────
FROM nginx:alpine

COPY --from=builder /app/dist /usr/share/nginx/html
COPY nginx.conf /etc/nginx/conf.d/default.conf

EXPOSE 80

CMD ["nginx", "-g", "daemon off;"]
