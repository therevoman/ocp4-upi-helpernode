FROM registry.access.redhat.com/rhel7

COPY . /app
RUN yum -y install librdmacm && yum clean all
CMD echo "First Container"
