FROM python:3.12-slim
WORKDIR /app
COPY lambda_api ./lambda_api
COPY lambda_ingest ./lambda_ingest
COPY local_server.py .
EXPOSE 8080
ENV HOST=0.0.0.0 PORT=8080
CMD ["python", "-u", "local_server.py"]
