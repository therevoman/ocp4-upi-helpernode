FROM registry.redhat.io/ubi8/ubi

USER root

COPY app /app
# Copy entitlements
COPY entitlement/* /etc/pki/entitlement/
# Copy subscription manager configurations
COPY rhsm/rhsm.conf /etc/rhsm/
COPY rhsm/ca /etc/rhsm/

ENV MOFED_VER MLNX_OFED_LINUX-4.7-1.0.0.1-rhel8.0-x86_64
ENV ADDITIONAL_REPOS rhel-8-for-x86_64-baseos-rpms

COPY ${MOFED_VER}.tgz /app

RUN ls /app
# Delete /etc/rhsm-host to use entitlements from the build container
RUN rm /etc/rhsm-host && \
    # Initialize /etc/yum.repos.d/redhat.repo
    # See https://access.redhat.com/solutions/1443553
    #yum repolist --disablerepo=* && \
    subscription-manager repos --enable ${ADDITIONAL_REPOS} && \
    yum -y update && \
    yum -y install tcsh numactl-libs && \
    # Remove entitlements and Subscription Manager configs
    rm -rf /etc/pki/entitlement && \
    rm -rf /etc/rhsm
# OpenShift requires images to run as non-root by default
#COPY mlnx.repo /etc/yum.repos.d/


RUN yum install perl python36 ethtool lsof pciutils iproute -y && \
	yum clean all
RUN yum install -y tcsh numactl-libs gcc-gfortran tk tcl && \
	yum clean all
#RUN yum install -y kernel-devel-* kernel-headers perl gcc make elfutils-libelf-devel

RUN cd /app && \
	tar -xzvf ${MOFED_VER}.tgz && \
	cd ${MOFED_VER} && \
	./mlnxofedinstall --all -vv --skip-repo --force --user-space-only --without-fw-update && \
	rm -rf /app/${MOFED_VER}
##	./mlnxofedinstall -vv --skip-repo --force --guest
##	./mlnxofedinstall --all --add-kernel-support -vv --skip-repo
#RUN yum install -y mlnx-en-vma
#RUN yum install -y perftest librdmacm-utils && yum clean all

RUN cd /app && make clean && make

#EXPOSE 54321 54322 54323 54324
USER 1001

CMD /app/RunImageReceiver.sh
#CMD sleep infinity

