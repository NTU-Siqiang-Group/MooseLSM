# running command: docker run --rm -p 3000:3000 -d rocksdbenv:web
FROM node:20.10.0-alpine

WORKDIR /App
ADD moose /App

RUN npm install && \
    npm run build && \
    npm install -g serve

CMD ["serve", "-s", "build"]