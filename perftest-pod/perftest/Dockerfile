FROM registry.redhat.io/ubi8/ubi
#FROM registry.redhat.io/rhel7:7.6-115
#FROM registry.access.redhat.com/ubi7/ubi

COPY . /app

USER root

# libraries from https://community.mellanox.com/s/article/howto-create-docker-container-enabled-with-roce
# multiple lines to make fewer layers (docker trick)
#RUN echo yum install -y libibverbs-utils \
#	libibverbs-devel \
#	libibverbs-devel-static \
#	libmlx4 \
#	libmlx5 \
#	ibutils \
#	libibcm \
#	libibcommon \
#	libibmad \
#	libibumad \
#	&& yum clean all

#try this one later.
#RUN yum install -y rdma librdmacm-utils librdmacm-devel librdmacm libibumad-devel perftest && yum clean all
#RUN yum -y install librdmacm && \
#	yum clean all
#
#RUN yum install kernel-devel -y
#RUN cd /app && \
#	tar -xzvf  mlnx-en-4.6-1.0.1.1-rhel8.0-x86_64.tgz && \
#	cd  mlnx-en-4.6-1.0.1.1-rhel8.0-x86_64 && \
#	./install -vv  --skip-kmp-verify --force
#RUN cd /app && \
#	tar -xzvf MLNX_OFED_LINUX-4.6-1.0.1.1-rhel7.6-x86_64.tgz && \
#	cd MLNX_OFED_LINUX-4.6-1.0.1.1-rhel7.6-x86_64 && \
#	./mlnxofedinstall -vv --skip-repo --force --guest
##	./mlnxofedinstall --all -vv --skip-repo
##	./mlnxofedinstall --all --add-kernel-support -vv --skip-repo
RUN yum --disableplugin=subscription-manager install -y perftest && yum clean all

EXPOSE 54321 54322 54323 54324

CMD /app/RunImageReceiver.sh
#CMD sleep infinity

