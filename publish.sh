docker build -f Dockerfile -t moosekv:server .
docker tag moosekv:server buchuitoudegou/moosekv:server
docker push buchuitoudegou/moosekv:server
cd front-end
docker build -f Dockerfile -t moosekv:frontend .
docker tag moosekv:frontend buchuitoudegou/moosekv:frontend
docker push buchuitoudegou/moosekv:frontend