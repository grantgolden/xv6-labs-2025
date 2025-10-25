#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "net.h"
#include "stdint.h"

// xv6's ethernet and IP addresses
static uint8 local_mac[ETHADDR_LEN] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
static uint32 local_ip = MAKE_IP_ADDR(10, 0, 2, 15);

// qemu host's ethernet address.
static uint8 host_mac[ETHADDR_LEN] = { 0x52, 0x55, 0x0a, 0x00, 0x02, 0x02 };

static struct spinlock netlock;

static struct udp_bind_recv udp_recv[16];

static int udp_recv_chan[16];           // &udp_recv_chan is the "wait channel"

void
netinit(void)
{
  initlock(&netlock, "netlock");
  memset(udp_recv, 0, sizeof(udp_recv));
}


//
// bind(int port)
// prepare to receive UDP packets address to the port,
// i.e. allocate any queues &c needed.
//
uint64
sys_bind(void)
{
  //
  // Your code here.
  //
  int dport;
  argint(0, &dport);
  int i;
  int udp_port_num = sizeof(udp_recv)/sizeof(udp_recv[0]);
  int first_unused = -1;
  acquire(&netlock);
  for (i = 0; i < udp_port_num; i++) {
      if (udp_recv[i].used && (udp_recv[i].dport == dport)) {
          release(&netlock);
          printf("udp port %d has already binded\n", dport);
          return -1;
      }

      if (!udp_recv[i].used && (first_unused == -1)) {
          first_unused = i;
      }
  }

  if (first_unused == -1) {
      release(&netlock);
      printf("no aviable port for binding\n");
      return -1;
  }

  memset(&udp_recv[first_unused], 0, sizeof(struct udp_bind_recv));

  udp_recv[first_unused].dport = dport;
  udp_recv[first_unused].used = 1;
  release(&netlock);

  return 0;
}

//
// unbind(int port)
// release any resources previously created by bind(port);
// from now on UDP packets addressed to port should be dropped.
//
uint64
sys_unbind(void)
{
  //
  // Optional: Your code here.
  //
  int dport;
  argint(0, &dport);
  acquire(&netlock);
  int i;
  for (i = 0; i < sizeof(udp_recv)/sizeof(udp_recv[0]); i++) {
      if (udp_recv[i].used && (udp_recv[i].dport == dport)) {
          udp_recv[i].used = 0;
          release(&netlock);
          return 0;
      }
  }

  release(&netlock);
  printf("port %d is not binded\n", dport);

  return -1;
}

//
// recv(int dport, int *src, short *sport, char *buf, int maxlen)
// if there's a received UDP packet already queued that was
// addressed to dport, then return it.
// otherwise wait for such a packet.
//
// sets *src to the IP source address.
// sets *sport to the UDP source port.
// copies up to maxlen bytes of UDP payload to buf.
// returns the number of bytes copied,
// and -1 if there was an error.
//
// dport, *src, and *sport are host byte order.
// bind(dport) must previously have been called.
//
uint64
sys_recv(void)
{
  //
  // Your code here.
  //
  struct proc *p = myproc();

  int dport;
  uint64 src;
  uint64 sport;
  uint64 user_buf;
  int maxlen;

  argint(0, &dport);
  argaddr(1, &src);
  argaddr(2, &sport);
  argaddr(3, &user_buf);
  argint(4, &maxlen);

  acquire(&netlock);

  int i;
  for (i = 0; i < sizeof(udp_recv)/sizeof(udp_recv[0]); i++) {
    //printf("used: %d, dport: %u\n", udp_recv[i].used, udp_recv[i].dport);
    if ((udp_recv[i].used == 1) && (udp_recv[i].dport == dport))
      break;
  }

  if (i == sizeof(udp_recv)/sizeof(udp_recv[0])) {
    release(&netlock);
    printf("no port binded, i: %d, dprot: %u\n", i, dport);
    return -1;
  }

  //wait queue not empty
  while (udp_recv[i].tail == udp_recv[i].head) {
    //udp queue is empty
    sleep(&udp_recv_chan[i], &netlock);
  }

  char *buf = (char*)udp_recv[i].pinfo[udp_recv[i].head].buf;
  int len = udp_recv[i].pinfo[udp_recv[i].head].length;

  udp_recv[i].head = (udp_recv[i].head+1)%(UDP_QUEUE_SIZE+1);

  release(&netlock);

  //printf("udp packet available\n");

  //copy src ip addr
  struct ip *ip = (struct ip *)(buf + sizeof(struct eth));
  uint32 ip_src = ntohl(ip->ip_src);

  if (copyout(p->pagetable, src, (char*)(uintptr_t)(&ip_src), sizeof(ip->ip_src)) < 0) {
    kfree(buf);
    printf("recv: copyout udp src ip failed\n");
    return -1;
  }

  //copy src port
  struct udp *udp = (struct udp *)(ip + 1);

  uint16 udp_sport = ntohs(udp->sport);
  if (copyout(p->pagetable, sport, (char*)(uintptr_t)(&udp_sport), sizeof(udp->sport)) < 0) {
    kfree(buf);
    printf("recv: copyout udp sport failed\n");
    return -1;
  }

  unsigned paysize = ntohs(udp->ulen) - sizeof(struct udp);
  len = maxlen > paysize ?  paysize : maxlen;

  //copy udp payload to user buf
  if (copyout(p->pagetable, user_buf, (char*)(udp+1), len) < 0) {
    kfree(buf);
    printf("recv: copyout udp payload failed, len = %x\n", len);
    return -1;
  }

  kfree(buf);
  return len;
}

// This code is lifted from FreeBSD's ping.c, and is copyright by the Regents
// of the University of California.
static unsigned short
in_cksum(const unsigned char *addr, int len)
{
  int nleft = len;
  const unsigned short *w = (const unsigned short *)addr;
  unsigned int sum = 0;
  unsigned short answer = 0;

  /*
   * Our algorithm is simple, using a 32 bit accumulator (sum), we add
   * sequential 16 bit words to it, and at the end, fold back all the
   * carry bits from the top 16 bits into the lower 16 bits.
   */
  while (nleft > 1)  {
    sum += *w++;
    nleft -= 2;
  }

  /* mop up an odd byte, if necessary */
  if (nleft == 1) {
    *(unsigned char *)(&answer) = *(const unsigned char *)w;
    sum += answer;
  }

  /* add back carry outs from top 16 bits to low 16 bits */
  sum = (sum & 0xffff) + (sum >> 16);
  sum += (sum >> 16);
  /* guaranteed now that the lower 16 bits of sum are correct */

  answer = ~sum; /* truncate to 16 bits */
  return answer;
}

//
// send(int sport, int dst, int dport, char *buf, int len)
//
uint64
sys_send(void)
{
  struct proc *p = myproc();
  int sport;
  int dst;
  int dport;
  uint64 bufaddr;
  int len;

  argint(0, &sport);
  argint(1, &dst);
  argint(2, &dport);
  argaddr(3, &bufaddr);
  argint(4, &len);

  int total = len + sizeof(struct eth) + sizeof(struct ip) + sizeof(struct udp);
  if(total > PGSIZE)
    return -1;

  char *buf = kalloc();
  if(buf == 0){
    printf("sys_send: kalloc failed\n");
    return -1;
  }
  memset(buf, 0, PGSIZE);

  struct eth *eth = (struct eth *) buf;
  memmove(eth->dhost, host_mac, ETHADDR_LEN);
  memmove(eth->shost, local_mac, ETHADDR_LEN);
  eth->type = htons(ETHTYPE_IP);

  struct ip *ip = (struct ip *)(eth + 1);
  ip->ip_vhl = 0x45; // version 4, header length 4*5
  ip->ip_tos = 0;
  ip->ip_len = htons(sizeof(struct ip) + sizeof(struct udp) + len);
  ip->ip_id = 0;
  ip->ip_off = 0;
  ip->ip_ttl = 100;
  ip->ip_p = IPPROTO_UDP;
  ip->ip_src = htonl(local_ip);
  ip->ip_dst = htonl(dst);
  ip->ip_sum = in_cksum((unsigned char *)ip, sizeof(*ip));

  struct udp *udp = (struct udp *)(ip + 1);
  udp->sport = htons(sport);
  udp->dport = htons(dport);
  udp->ulen = htons(len + sizeof(struct udp));

  char *payload = (char *)(udp + 1);
  if(copyin(p->pagetable, payload, bufaddr, len) < 0){
    kfree(buf);
    printf("send: copyin failed\n");
    return -1;
  }

  e1000_transmit(buf, total);

  return 0;
}

void
ip_rx(char *buf, int len)
{
  // don't delete this printf; make grade depends on it.
  static int seen_ip = 0;
  if(seen_ip == 0)
    printf("ip_rx: received an IP packet\n");
  seen_ip = 1;

  //
  // Your code here.
  //
  struct ip *ip = (struct ip*)(buf + sizeof(struct eth));

  if (ip->ip_p == IPPROTO_UDP) {
    //printf("udp received\n");
    struct udp *udp = (struct udp *)(ip + 1);
    unsigned dport = ntohs(udp->dport);
    int i;
    acquire(&netlock);
    for (i = 0; i < sizeof(udp_recv)/sizeof(udp_recv[0]); i++) {
        if (udp_recv[i].used && udp_recv[i].dport == dport)
            break;
    }

    if (i == sizeof(udp_recv)/sizeof(udp_recv[0])) {
        release(&netlock);
        kfree(buf);
        return;
    }

    //both udp and dport match, and queues not full, packet enqueue
    if (((udp_recv[i].tail+1)%(UDP_QUEUE_SIZE+1)) != udp_recv[i].head) {
        udp_recv[i].pinfo[udp_recv[i].tail].buf = buf;
        udp_recv[i].pinfo[udp_recv[i].tail].length= len;
        udp_recv[i].tail = (udp_recv[i].tail+1)%(UDP_QUEUE_SIZE+1);
        wakeup(&udp_recv_chan[i]);
        release(&netlock);
        //printf("udp packet enqueue\n");
        return;
    } else {
        release(&netlock);
        kfree(buf);
        //printf("udp queue full, udp_recv[%d]: tail->%u, head->%u, discarded!\n", i, udp_recv[i].tail, udp_recv[i].head);
    }
  } else {
    kfree(buf);
  }
}

//
// send an ARP reply packet to tell qemu to map
// xv6's ip address to its ethernet address.
// this is the bare minimum needed to persuade
// qemu to send IP packets to xv6; the real ARP
// protocol is more complex.
//
void
arp_rx(char *inbuf)
{
  static int seen_arp = 0;

  if(seen_arp){
    kfree(inbuf);
    return;
  }
  printf("arp_rx: received an ARP packet\n");
  seen_arp = 1;

  struct eth *ineth = (struct eth *) inbuf;
  struct arp *inarp = (struct arp *) (ineth + 1);

  char *buf = kalloc();
  if(buf == 0)
    panic("send_arp_reply");

  struct eth *eth = (struct eth *) buf;
  memmove(eth->dhost, ineth->shost, ETHADDR_LEN); // ethernet destination = query source
  memmove(eth->shost, local_mac, ETHADDR_LEN); // ethernet source = xv6's ethernet address
  eth->type = htons(ETHTYPE_ARP);

  struct arp *arp = (struct arp *)(eth + 1);
  arp->hrd = htons(ARP_HRD_ETHER);
  arp->pro = htons(ETHTYPE_IP);
  arp->hln = ETHADDR_LEN;
  arp->pln = sizeof(uint32);
  arp->op = htons(ARP_OP_REPLY);

  memmove(arp->sha, local_mac, ETHADDR_LEN);
  arp->sip = htonl(local_ip);
  memmove(arp->tha, ineth->shost, ETHADDR_LEN);
  arp->tip = inarp->sip;

  e1000_transmit(buf, sizeof(*eth) + sizeof(*arp));

  kfree(inbuf);
}

void
net_rx(char *buf, int len)
{
  struct eth *eth = (struct eth *) buf;

  if(len >= sizeof(struct eth) + sizeof(struct arp) &&
     ntohs(eth->type) == ETHTYPE_ARP){
    arp_rx(buf);
  } else if(len >= sizeof(struct eth) + sizeof(struct ip) &&
     ntohs(eth->type) == ETHTYPE_IP){
    ip_rx(buf, len);
  } else {
    kfree(buf);
  }
}
