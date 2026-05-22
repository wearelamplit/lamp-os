<script setup lang="ts">
import LamplitLogo from '@/components/LamplitLogo.vue'
</script>

<template>
  <section class="tab-panel" aria-label="Information">
    <div class="info-content">
      <div class="logo-container">
        <LamplitLogo />
      </div>
      <p>
        Lamplit Art Society is a non-profit collective dedicated to sparking inspiration and
        providing opportunities for people to connect, celebrate, grow, and inspire others through
        shared creative experiences.
      </p>
      <p>
        The lamps are the art project from which our society grew. Their surreal and vivid presence
        captivates audiences, fosters unexpected connections, inspires creativity and play, and
        illuminates spaces.
      </p>
      <p>
        As stewards of this decentralized and open source project, we maintain its core vision while
        welcoming contributors and artists to build, adopt, or share these lamps with their
        communities.
      </p>
      <p>Find more info at <b>lamplit.ca</b></p>
    </div>
  </section>
</template>

<style scoped>
.tab-panel {
  animation: fadeIn 0.3s ease-in-out;
}

@keyframes fadeIn {
  from {
    opacity: 0;
    transform: translateY(10px);
  }
  to {
    opacity: 1;
    transform: translateY(0);
  }
}

.info-content {
  padding: 20px;
  color: var(--brand-slate-grey);
}

.logo-container {
  display: flex;
  justify-content: center;
  margin-bottom: 24px;
}

.logo {
  width: 200px;
  max-width: 40%;
  height: auto;
  border-radius: 8px;
}

.info-content h2 {
  color: var(--brand-fog-grey);
  margin-bottom: 16px;
  font-size: 1.5rem;
  font-weight: 600;
}

.info-content h3 {
  color: var(--brand-fog-grey);
  margin: 24px 0 12px 0;
  font-size: 1.1rem;
  font-weight: 500;
}

.info-content p {
  line-height: 1.6;
  margin-bottom: 16px;
}

.info-content ul {
  margin: 0 0 16px 0;
  padding-left: 20px;
}

.info-content li {
  margin-bottom: 8px;
  line-height: 1.5;
}
</style>

